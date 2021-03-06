
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2013 Francois Beaune, Jupiter Jazz Limited
// Copyright (c) 2014-2017 Francois Beaune, The appleseedhq Organization
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

// Interface header.
#include "lighttracingsamplegenerator.h"

// appleseed.renderer headers.
#include "renderer/global/globallogger.h"
#include "renderer/global/globaltypes.h"
#include "renderer/kernel/intersection/intersector.h"
#include "renderer/kernel/lighting/lightsampler.h"
#include "renderer/kernel/lighting/pathtracer.h"
#include "renderer/kernel/lighting/pathvertex.h"
#include "renderer/kernel/lighting/scatteringmode.h"
#include "renderer/kernel/lighting/tracer.h"
#include "renderer/kernel/rendering/globalsampleaccumulationbuffer.h"
#include "renderer/kernel/rendering/sample.h"
#include "renderer/kernel/rendering/samplegeneratorbase.h"
#include "renderer/kernel/shading/oslshadergroupexec.h"
#include "renderer/kernel/shading/shadingcontext.h"
#include "renderer/kernel/shading/shadingpoint.h"
#include "renderer/kernel/shading/shadingray.h"
#include "renderer/kernel/texturing/texturecache.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/camera/camera.h"
#include "renderer/modeling/edf/edf.h"
#include "renderer/modeling/environment/environment.h"
#include "renderer/modeling/environmentedf/environmentedf.h"
#include "renderer/modeling/frame/frame.h"
#include "renderer/modeling/light/light.h"
#include "renderer/modeling/material/material.h"
#include "renderer/modeling/project/project.h"
#include "renderer/modeling/scene/scene.h"
#include "renderer/modeling/scene/visibilityflags.h"
#include "renderer/utility/settingsparsing.h"
#include "renderer/utility/transformsequence.h"

// appleseed.foundation headers.
#include "foundation/image/canvasproperties.h"
#include "foundation/image/color.h"
#include "foundation/image/image.h"
#include "foundation/math/basis.h"
#include "foundation/math/population.h"
#include "foundation/math/sampling/mappings.h"
#include "foundation/math/scalar.h"
#include "foundation/math/transform.h"
#include "foundation/math/vector.h"
#include "foundation/platform/types.h"
#include "foundation/utility/arena.h"
#include "foundation/utility/statistics.h"
#include "foundation/utility/string.h"

// Standard headers.
#include <cassert>
#include <cmath>
#include <string>

// Forward declarations.
namespace foundation    { class IAbortSwitch; }
namespace foundation    { class LightingConditions; }

using namespace foundation;

namespace renderer
{

namespace
{
    //
    // LightTracingSampleGenerator class implementation.
    //
    // References:
    //
    //   Monte Carlo Light Tracing With Direct Computation Of Pixel Intensities
    //   http://graphics.cs.kuleuven.be/publications/MCLTWDCOPI/
    //
    //   Robust Monte Carlo Methods For Light Transport Simulation
    //   http://graphics.stanford.edu/papers/veach_thesis/thesis.pdf
    //

    class LightTracingSampleGenerator
      : public SampleGeneratorBase
    {
      public:
        struct Parameters
        {
            const SamplingContext::Mode m_sampling_mode;

            const bool                  m_enable_ibl;                   // is image-based lighting enabled?
            const bool                  m_enable_caustics;              // are caustics enabled?

            const float                 m_transparency_threshold;
            const size_t                m_max_iterations;
            const bool                  m_report_self_intersections;

            const size_t                m_max_path_length;              // maximum path length, ~0 for unlimited
            const size_t                m_rr_min_path_length;           // minimum path length before Russian Roulette kicks in, ~0 for unlimited

            explicit Parameters(const ParamArray& params)
              : m_sampling_mode(get_sampling_context_mode(params))
              , m_enable_ibl(params.get_optional<bool>("enable_ibl", true))
              , m_enable_caustics(params.get_optional<bool>("enable_caustics", true))
              , m_transparency_threshold(params.get_optional<float>("transparency_threshold", 0.001f))
              , m_max_iterations(params.get_optional<size_t>("max_iterations", 1000))
              , m_report_self_intersections(params.get_optional<bool>("report_self_intersections", false))
              , m_max_path_length(nz(params.get_optional<size_t>("max_path_length", 0)))
              , m_rr_min_path_length(nz(params.get_optional<size_t>("rr_min_path_length", 3)))
            {
            }

            static size_t nz(const size_t x)
            {
                return x == 0 ? ~0 : x;
            }

            void print() const
            {
                RENDERER_LOG_INFO(
                    "light tracing settings:\n"
                    "  ibl              %s\n"
                    "  caustics         %s\n"
                    "  max path length  %s\n"
                    "  rr min path len. %s",
                    m_enable_ibl ? "on" : "off",
                    m_enable_caustics ? "on" : "off",
                    m_max_path_length == size_t(~0) ? "infinite" : pretty_uint(m_max_path_length).c_str(),
                    m_rr_min_path_length == size_t(~0) ? "infinite" : pretty_uint(m_rr_min_path_length).c_str());
            }
        };

        LightTracingSampleGenerator(
            const Project&              project,
            const Frame&                frame,
            const TraceContext&         trace_context,
            TextureStore&               texture_store,
            const LightSampler&         light_sampler,
            const size_t                generator_index,
            const size_t                generator_count,
            OIIO::TextureSystem&        oiio_texture_system,
            OSL::ShadingSystem&         shading_system,
            const ParamArray&           params)
          : SampleGeneratorBase(generator_index, generator_count)
          , m_params(params)
          , m_scene(*project.get_scene())
          , m_frame(frame)
          , m_light_sampler(light_sampler)
          , m_texture_cache(texture_store)
          , m_intersector(trace_context, m_texture_cache, m_params.m_report_self_intersections)
          , m_shadergroup_exec(shading_system, m_arena)
          , m_tracer(
                m_scene,
                m_intersector,
                m_texture_cache,
                m_shadergroup_exec,
                m_params.m_transparency_threshold,
                m_params.m_max_iterations)
          , m_shading_context(
                m_intersector,
                m_tracer,
                m_texture_cache,
                oiio_texture_system,
                m_shadergroup_exec,
                m_arena,
                generator_index,
                0,
                m_params.m_transparency_threshold,
                m_params.m_max_iterations)
          , m_light_sample_count(0)
          , m_path_count(0)
        {
            const Scene::RenderData& scene_data = m_scene.get_render_data();
            m_scene_center = Vector3d(scene_data.m_center);
            m_scene_radius = scene_data.m_radius;
            m_safe_scene_diameter = scene_data.m_safe_diameter;
            m_disk_point_prob = 1.0f / (Pi<float>() * square(static_cast<float>(m_scene_radius)));

            const Camera* camera = project.get_uncached_active_camera();

            m_shutter_open_time = camera->get_shutter_open_time();
            m_shutter_close_time = camera->get_shutter_close_time();
        }

        virtual void release() APPLESEED_OVERRIDE
        {
            delete this;
        }

        virtual void reset() APPLESEED_OVERRIDE
        {
            SampleGeneratorBase::reset();
            m_rng = SamplingContext::RNGType();
        }

        virtual void generate_samples(
            const size_t                sample_count,
            SampleAccumulationBuffer&   buffer,
            IAbortSwitch&               abort_switch) APPLESEED_OVERRIDE
        {
            m_light_sample_count = 0;

            SampleGeneratorBase::generate_samples(sample_count, buffer, abort_switch);

            static_cast<GlobalSampleAccumulationBuffer&>(buffer)
                .increment_sample_count(m_light_sample_count);
        }

        virtual StatisticsVector get_statistics() const APPLESEED_OVERRIDE
        {
            Statistics stats;
            stats.insert("path count", m_path_count);
            stats.insert("path length", m_path_length);

            return StatisticsVector::make("light tracing statistics", stats);
        }

      private:
        struct PathVisitor
        {
            const Parameters&               m_params;
            const Camera&                   m_camera;
            const Frame&                    m_frame;
            const LightingConditions&       m_lighting_conditions;
            const ShadingContext&           m_shading_context;
            SamplingContext&                m_sampling_context;

            const Spectrum                  m_initial_flux;         // initial particle flux (in W)
            SampleVector&                   m_samples;
            size_t                          m_sample_count;         // the number of samples added to m_samples

            PathVisitor(
                const Parameters&           params,
                const Scene&                scene,
                const Frame&                frame,
                const ShadingContext&       shading_context,
                SamplingContext&            sampling_context,
                SampleVector&               samples,
                const Spectrum&             initial_flux)
              : m_params(params)
              , m_camera(*scene.get_active_camera())
              , m_frame(frame)
              , m_lighting_conditions(frame.get_lighting_conditions())
              , m_shading_context(shading_context)
              , m_sampling_context(sampling_context)
              , m_samples(samples)
              , m_sample_count(0)
              , m_initial_flux(initial_flux)
            {
            }

            size_t get_sample_count() const
            {
                return m_sample_count;
            }

            bool accept_scattering(
                const ScatteringMode::Mode  prev_mode,
                const ScatteringMode::Mode  next_mode) const
            {
                assert(next_mode != ScatteringMode::Absorption);

                if (!m_params.m_enable_caustics)
                {
                    // Don't follow paths leading to caustics.
                    if (ScatteringMode::has_glossy_or_specular(next_mode))
                        return false;
                }

                return true;
            }

            void visit_area_light_vertex(
                const LightSample&          light_sample,
                const Spectrum&             light_particle_flux,
                const ShadingRay::Time&     time)
            {
                // Connect the light vertex with the camera.
                Vector2d sample_position;
                Vector3d camera_outgoing;
                float importance;
                if (!m_camera.connect_vertex(
                        m_sampling_context,
                        time.m_absolute,
                        light_sample.m_point,
                        sample_position,
                        camera_outgoing,
                        importance))
                    return;

                // Reject vertices on the back side of the area light.
                double cos_alpha = dot(-camera_outgoing, light_sample.m_shading_normal);
                if (cos_alpha <= 0.0)
                    return;

                // Compute the transmission factor between the light vertex and the camera.
                // Prevent self-intersections by letting the ray originate from the camera.
                const float transmission =
                    m_shading_context.get_tracer().trace_between(
                        light_sample.m_point - camera_outgoing,
                        light_sample.m_point,
                        time,
                        VisibilityFlags::CameraRay,
                        0);

                // Ignore occluded vertices.
                if (transmission == 0.0f)
                    return;

                // Adjust cos(alpha) to account for the fact that the camera outgoing direction was not unit-length.
                const double distance = norm(camera_outgoing);
                cos_alpha /= distance;

                // Store the contribution of this vertex.
                Spectrum radiance = light_particle_flux;
                radiance *= static_cast<float>(transmission * cos_alpha * importance);
                emit_sample(sample_position, distance, radiance);
            }

            void visit_non_physical_light_vertex(
                const Vector3d&             light_vertex,
                const Spectrum&             light_particle_flux,
                const ShadingRay::Time&     time)
            {
                // Connect the light vertex with the camera.
                Vector2d sample_position;
                Vector3d camera_outgoing;
                float importance;
                if (!m_camera.connect_vertex(
                        m_sampling_context,
                        time.m_absolute,
                        light_vertex,
                        sample_position,
                        camera_outgoing,
                        importance))
                    return;

                // Compute the transmission factor between the light vertex and the camera.
                const float transmission =
                    m_shading_context.get_tracer().trace_between(
                        light_vertex - camera_outgoing,
                        light_vertex,
                        time,
                        VisibilityFlags::CameraRay,
                        0);

                // Ignore occluded vertices.
                if (transmission == 0.0f)
                    return;

                // Store the contribution of this vertex.
                Spectrum radiance = light_particle_flux;
                radiance *= transmission * importance;
                emit_sample(sample_position, norm(camera_outgoing), radiance);
            }

            void visit_vertex(const PathVertex& vertex)
            {
                // Don't process this vertex if there is no BSDF.
                if (vertex.m_bsdf == 0)
                    return;

                // Connect the path vertex with the camera.
                Vector2d sample_position;
                Vector3d camera_outgoing;
                float importance;
                if (!m_camera.connect_vertex(
                        m_sampling_context,
                        vertex.get_time().m_absolute,
                        vertex.get_point(),
                        sample_position,
                        camera_outgoing,
                        importance))
                    return;

                // Reject vertices on the back side of the shading surface.
                const Vector3d& shading_normal = vertex.get_shading_normal();
                if (dot(camera_outgoing, shading_normal) >= 0.0)
                    return;

                // Compute the transmission factor between the path vertex and the camera.
                // Prevent self-intersections by letting the ray originate from the camera.
                const float transmission =
                    m_shading_context.get_tracer().trace_between(
                        vertex.get_point() - camera_outgoing,
                        vertex.get_point(),
                        vertex.get_time(),
                        VisibilityFlags::CameraRay,
                        static_cast<ShadingRay::DepthType>(vertex.m_path_length));  // ray depth = (path length - 1) + 1

                // Ignore occluded vertices.
                if (transmission == 0.0f)
                    return;

                // Normalize the camera outgoing direction.
                const double distance = norm(camera_outgoing);
                camera_outgoing /= distance;

                // Retrieve the geometric normal at the vertex.
                const Vector3d geometric_normal =
                    flip_to_same_hemisphere(
                        vertex.get_geometric_normal(),
                        shading_normal);

                // Evaluate the BSDF at the vertex position.
                Spectrum bsdf_value;
                const float bsdf_prob =
                    vertex.m_bsdf->evaluate(
                        vertex.m_bsdf_data,
                        true,                                       // adjoint
                        true,                                       // multiply by |cos(incoming, normal)|
                        Vector3f(geometric_normal),
                        Basis3f(vertex.get_shading_basis()),
                        Vector3f(vertex.m_outgoing.get_value()),    // outgoing (toward the light)
                        -Vector3f(camera_outgoing),                 // incoming (toward the camera)
                        ScatteringMode::All,                        // todo: likely incorrect
                        bsdf_value);
                if (bsdf_prob == 0.0f)
                    return;

                // Store the contribution of this vertex.
                Spectrum radiance = m_initial_flux;
                radiance *= vertex.m_throughput;
                radiance *= bsdf_value;
                radiance *= transmission * importance;
                emit_sample(sample_position, distance, radiance);
            }

            void visit_environment(const PathVertex& vertex)
            {
                // The particle escapes.
            }

            void emit_sample(
                const Vector2d&             position_ndc,
                const double                distance,
                const Spectrum&             radiance)
            {
                assert(min_value(radiance) >= 0.0f);

                const Color3f linear_rgb =
                    radiance.is_rgb()
                        ? radiance.rgb()
                        : radiance.convert_to_rgb(m_lighting_conditions);

                Sample sample;
                sample.m_position = Vector2f(position_ndc);
                sample.m_values[0] = linear_rgb.r;
                sample.m_values[1] = linear_rgb.g;
                sample.m_values[2] = linear_rgb.b;
                sample.m_values[3] = 1.0f;
                sample.m_values[4] = static_cast<float>(distance);
                m_samples.push_back(sample);

                ++m_sample_count;
            }
        };

        typedef PathTracer<PathVisitor, true> PathTracerType;   // true = adjoint

        const Parameters                m_params;

        const Scene&                    m_scene;
        const Frame&                    m_frame;

        // Preserve order.
        Vector3d                        m_scene_center;         // world space
        double                          m_scene_radius;         // world space
        double                          m_safe_scene_diameter;  // world space
        float                           m_disk_point_prob;

        const LightSampler&             m_light_sampler;
        TextureCache                    m_texture_cache;
        Intersector                     m_intersector;
        Arena                           m_arena;
        OSLShaderGroupExec              m_shadergroup_exec;
        Tracer                          m_tracer;
        const ShadingContext            m_shading_context;

        SamplingContext::RNGType        m_rng;

        uint64                          m_light_sample_count;

        uint64                          m_path_count;
        Population<uint64>              m_path_length;

        float                           m_shutter_open_time;
        float                           m_shutter_close_time;

        virtual size_t generate_samples(
            const size_t                sequence_index,
            SampleVector&               samples) APPLESEED_OVERRIDE
        {
            m_arena.clear();

            SamplingContext sampling_context(
                m_rng,
                m_params.m_sampling_mode,
                0,
                sequence_index,
                sequence_index);

            size_t stored_sample_count = 0;

            if (m_light_sampler.has_lights_or_emitting_triangles())
                stored_sample_count += generate_light_sample(sampling_context, samples);

            if (m_params.m_enable_ibl)
            {
                const EnvironmentEDF* env_edf = m_scene.get_environment()->get_environment_edf();

                if (env_edf)
                {
                    stored_sample_count +=
                        generate_environment_sample(sampling_context, env_edf, samples);
                }
            }

            ++m_light_sample_count;

            return stored_sample_count;
        }

        size_t generate_light_sample(
            SamplingContext&            sampling_context,
            SampleVector&               samples)
        {
            // Sample the light sources.
            sampling_context.split_in_place(4, 1);
            const Vector4f s = sampling_context.next2<Vector4f>();
            LightSample light_sample;
            m_light_sampler.sample(
                ShadingRay::Time::create_with_normalized_time(
                    s[0],
                    m_shutter_open_time,
                    m_shutter_close_time),
                Vector3f(s[1], s[2], s[3]),
                light_sample);

            return
                light_sample.m_triangle
                    ? generate_emitting_triangle_sample(sampling_context, light_sample, samples)
                    : generate_non_physical_light_sample(sampling_context, light_sample, samples);
        }

        size_t generate_emitting_triangle_sample(
            SamplingContext&            sampling_context,
            LightSample&                light_sample,
            SampleVector&               samples)
        {
            // Make sure the geometric normal of the light sample is in the same hemisphere as the shading normal.
            light_sample.m_geometric_normal =
                flip_to_same_hemisphere(
                    light_sample.m_geometric_normal,
                    light_sample.m_shading_normal);

            const Material* material = light_sample.m_triangle->m_material;
            const Material::RenderData& material_data = material->get_render_data();

            // Build a shading point on the light source.
            ShadingPoint light_shading_point;
            light_sample.make_shading_point(
                light_shading_point,
                light_sample.m_shading_normal,
                m_shading_context.get_intersector());

            if (material_data.m_shader_group)
            {
                m_shading_context.execute_osl_emission(
                    *material_data.m_shader_group,
                    light_shading_point);
            }

            // Sample the EDF.
            sampling_context.split_in_place(2, 1);
            Vector3f emission_direction;
            Spectrum edf_value(Spectrum::Illuminance);
            float edf_prob;
            material_data.m_edf->sample(
                sampling_context,
                material_data.m_edf->evaluate_inputs(m_shading_context, light_shading_point),
                Vector3f(light_sample.m_geometric_normal),
                Basis3f(Vector3f(light_sample.m_shading_normal)),
                sampling_context.next2<Vector2f>(),
                emission_direction,
                edf_value,
                edf_prob);

            // Compute the initial particle weight.
            Spectrum initial_flux = edf_value;
            initial_flux *=
                dot(emission_direction, Vector3f(light_sample.m_shading_normal)) /
                (light_sample.m_probability * edf_prob);

            // Make a shading point that will be used to avoid self-intersections with the light sample.
            ShadingPoint parent_shading_point;
            light_sample.make_shading_point(
                parent_shading_point,
                Vector3d(emission_direction),
                m_intersector);

            // Build the light ray.
            sampling_context.split_in_place(1, 1);
            const ShadingRay::Time time =
                ShadingRay::Time::create_with_normalized_time(
                    sampling_context.next2<float>(),
                    m_shutter_open_time,
                    m_shutter_close_time);
            const ShadingRay light_ray(
                light_sample.m_point,
                Vector3d(emission_direction),
                time,
                VisibilityFlags::LightRay,
                0);

            // Build the path tracer.
            PathVisitor path_visitor(
                m_params,
                m_scene,
                m_frame,
                m_shading_context,
                sampling_context,
                samples,
                initial_flux);
            PathTracerType path_tracer(
                path_visitor,
                m_params.m_rr_min_path_length,
                m_params.m_max_path_length,
                m_params.m_max_iterations,
                material_data.m_edf->get_light_near_start());   // don't illuminate points closer than the light near start value

            // Handle the light vertex separately.
            Spectrum light_particle_flux = edf_value;           // todo: only works for diffuse EDF? What we need is the light exitance
            light_particle_flux /= light_sample.m_probability;
            path_visitor.visit_area_light_vertex(
                light_sample,
                light_particle_flux,
                light_ray.m_time);

            // Trace the light path.
            const size_t path_length =
                path_tracer.trace(
                    sampling_context,
                    m_shading_context,
                    light_ray,
                    &parent_shading_point);

            // Update path statistics.
            ++m_path_count;
            m_path_length.insert(path_length);

            // Return the number of samples generated when tracing this light path.
            return path_visitor.get_sample_count();
        }

        size_t generate_non_physical_light_sample(
            SamplingContext&            sampling_context,
            const LightSample&          light_sample,
            SampleVector&               samples)
        {
            // Sample the light.
            sampling_context.split_in_place(2, 1);
            Vector3d emission_position, emission_direction;
            Spectrum light_value(Spectrum::Illuminance);
            float light_prob;
            light_sample.m_light->sample(
                m_shading_context,
                light_sample.m_light_transform,
                sampling_context.next2<Vector2d>(),
                emission_position,
                emission_direction,
                light_value,
                light_prob);

            // Compute the initial particle weight.
            Spectrum initial_flux = light_value;
            initial_flux /= light_sample.m_probability * light_prob;

            // Build the light ray.
            sampling_context.split_in_place(1, 1);
            const ShadingRay::Time time =
                ShadingRay::Time::create_with_normalized_time(
                    sampling_context.next2<float>(),
                    m_shutter_open_time,
                    m_shutter_close_time);
            const ShadingRay light_ray(
                emission_position,
                emission_direction,
                time,
                VisibilityFlags::LightRay,
                0);

            // Build the path tracer.
            PathVisitor path_visitor(
                m_params,
                m_scene,
                m_frame,
                m_shading_context,
                sampling_context,
                samples,
                initial_flux);
            PathTracerType path_tracer(
                path_visitor,
                m_params.m_rr_min_path_length,
                m_params.m_max_path_length,
                m_params.m_max_iterations);

            // Handle the light vertex separately.
            Spectrum light_particle_flux = light_value;
            light_particle_flux /= light_sample.m_probability;
            path_visitor.visit_non_physical_light_vertex(
                emission_position,
                light_particle_flux,
                light_ray.m_time);

            // Trace the light path.
            const size_t path_length =
                path_tracer.trace(
                    sampling_context,
                    m_shading_context,
                    light_ray);

            // Update path statistics.
            ++m_path_count;
            m_path_length.insert(path_length);

            // Return the number of samples generated when tracing this light path.
            return path_visitor.get_sample_count();
        }

        size_t generate_environment_sample(
            SamplingContext&            sampling_context,
            const EnvironmentEDF*       env_edf,
            SampleVector&               samples)
        {
            // Sample the environment.
            sampling_context.split_in_place(2, 1);
            Vector3f outgoing;
            Spectrum env_edf_value(Spectrum::Illuminance);
            float env_edf_prob;
            env_edf->sample(
                m_shading_context,
                sampling_context.next2<Vector2f>(),
                outgoing,               // points toward the environment
                env_edf_value,
                env_edf_prob);

            // Uniformly sample the tangent disk.
            sampling_context.split_in_place(2, 1);
            const Vector2d p =
                  m_scene_radius
                * sample_disk_uniform(sampling_context.next2<Vector2d>());

            // Compute the origin of the light ray.
            const Basis3d basis(-Vector3d(outgoing));
            const Vector3d ray_origin =
                  m_scene_center
                - m_safe_scene_diameter * basis.get_normal()    // a safe radius would have been sufficient
                + p[0] * basis.get_tangent_u() +
                + p[1] * basis.get_tangent_v();

            // Compute the initial particle weight.
            Spectrum initial_flux = env_edf_value;
            initial_flux /= m_disk_point_prob * env_edf_prob;

            // Build the light ray.
            sampling_context.split_in_place(1, 1);
            const ShadingRay::Time time =
                ShadingRay::Time::create_with_normalized_time(
                    sampling_context.next2<float>(),
                    m_shutter_open_time,
                    m_shutter_close_time);
            const ShadingRay light_ray(
                ray_origin,
                -Vector3d(outgoing),
                time,
                VisibilityFlags::LightRay,
                0);

            // Build the path tracer.
            PathVisitor path_visitor(
                m_params,
                m_scene,
                m_frame,
                m_shading_context,
                sampling_context,
                samples,
                initial_flux);
            PathTracerType path_tracer(
                path_visitor,
                m_params.m_rr_min_path_length,
                m_params.m_max_path_length,
                m_params.m_max_iterations);

            // Trace the light path.
            const size_t path_length =
                path_tracer.trace(
                    sampling_context,
                    m_shading_context,
                    light_ray);

            // Update path statistics.
            ++m_path_count;
            m_path_length.insert(path_length);

            // Return the number of samples generated when tracing this light path.
            return path_visitor.get_sample_count();
        }
    };
}


//
// LightTracingSampleGeneratorFactory class implementation.
//

LightTracingSampleGeneratorFactory::LightTracingSampleGeneratorFactory(
    const Project&          project,
    const Frame&            frame,
    const TraceContext&     trace_context,
    TextureStore&           texture_store,
    const LightSampler&     light_sampler,
    OIIO::TextureSystem&    oiio_texture_system,
    OSL::ShadingSystem&     shading_system,
    const ParamArray&       params)
  : m_project(project)
  , m_frame(frame)
  , m_trace_context(trace_context)
  , m_texture_store(texture_store)
  , m_light_sampler(light_sampler)
  , m_oiio_texture_system(oiio_texture_system)
  , m_shading_system(shading_system)
  , m_params(params)
{
    LightTracingSampleGenerator::Parameters(params).print();
}

void LightTracingSampleGeneratorFactory::release()
{
    delete this;
}

ISampleGenerator* LightTracingSampleGeneratorFactory::create(
    const size_t            generator_index,
    const size_t            generator_count)
{
    return
        new LightTracingSampleGenerator(
            m_project,
            m_frame,
            m_trace_context,
            m_texture_store,
            m_light_sampler,
            generator_index,
            generator_count,
            m_oiio_texture_system,
            m_shading_system,
            m_params);
}

SampleAccumulationBuffer* LightTracingSampleGeneratorFactory::create_sample_accumulation_buffer()
{
    const CanvasProperties& props = m_frame.image().properties();

    return
        new GlobalSampleAccumulationBuffer(
            props.m_canvas_width,
            props.m_canvas_height,
            m_frame.get_filter());
}

}   // namespace renderer
