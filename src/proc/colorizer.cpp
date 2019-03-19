// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2017 Intel Corporation. All Rights Reserved.

#include "../include/librealsense2/hpp/rs_sensor.hpp"
#include "../include/librealsense2/hpp/rs_processing.hpp"

#include "proc/synthetic-stream.h"
#include "context.h"
#include "environment.h"
#include "option.h"
#include "colorizer.h"

namespace librealsense
{
	static color_map hue{ {
		{ 255, 0, 0 },
		{ 255, 255, 0 },
		{ 0, 255, 0 },
		{ 0, 255, 255 },
		{ 0, 0, 255 },
		{ 255, 0, 255 },
		{ 255, 0, 0 },
		} };

    static color_map jet{ {
        { 0, 0, 255 },
        { 0, 255, 255 },
        { 255, 255, 0 },
        { 255, 0, 0 },
        { 50, 0, 0 },
		} };

    static color_map classic{ {
        { 30, 77, 203 },
        { 25, 60, 192 },
        { 45, 117, 220 },
        { 204, 108, 191 },
        { 196, 57, 178 },
        { 198, 33, 24 },
        } };

    static color_map grayscale{ {
        { 255, 255, 255 },
        { 0, 0, 0 },
        } };

    static color_map inv_grayscale{ {
        { 0, 0, 0 },
        { 255, 255, 255 },
        } };

    static color_map biomes{ {
        { 0, 0, 204 },
        { 204, 230, 255 },
        { 255, 255, 153 },
        { 170, 255, 128 },
        { 0, 153, 0 },
        { 230, 242, 255 },
        } };

    static color_map cold{ {
        { 230, 247, 255 },
        { 0, 92, 230 },
        { 0, 179, 179 },
        { 0, 51, 153 },
        { 0, 5, 15 }
        } };

    static color_map warm{ {
        { 255, 255, 230 },
        { 255, 204, 0 },
        { 255, 136, 77 },
        { 255, 51, 0 },
        { 128, 0, 0 },
        { 10, 0, 0 }
        } };

    static color_map quantized{ {
        { 255, 255, 255 },
        { 0, 0, 0 },
        }, 6 };

    static color_map pattern{ {
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        { 255, 255, 255 },
        { 0, 0, 0 },
        } };

	void colorizer::update_disparity_histogram(int* hist, const float* depth_data, int w, int h)
	{
		memset(hist, 0, MAX_DEPTH * sizeof(int));
		for (auto i = 0; i < w*h; ++i) ++hist[(int)depth_data[i]];
		for (auto i = 2; i < MAX_DEPTH; ++i) hist[i] += hist[i - 1]; // Build a cumulative histogram for the indices in [1,0xFFFF]
	}

    void colorizer::update_histogram(int* hist, const uint16_t* depth_data, int w, int h)
    {
        memset(hist, 0, MAX_DEPTH * sizeof(int));
        for (auto i = 0; i < w*h; ++i) ++hist[depth_data[i]];
        for (auto i = 2; i < MAX_DEPTH; ++i) hist[i] += hist[i - 1]; // Build a cumulative histogram for the indices in [1,0xFFFF]
    }

    colorizer::colorizer()
        : stream_filter_processing_block("Depth Visualization"),
         _min(0.f), _max(6.f), _equalize(true), 
         _target_stream_profile(), _histogram(),
		_stereoscopic_depth(false),
		_focal_lenght_mm(0.f),
		_stereo_baseline_meter(0.f),
		_depth_units(0.f),
		_d2d_convert_factor(0.f)
    {
        _histogram = std::vector<int>(MAX_DEPTH, 0);
        _hist_data = _histogram.data();
        _stream_filter.stream = RS2_STREAM_DEPTH;
        _stream_filter.format = RS2_FORMAT_Z16;

        _maps = { &jet, &hue, &classic, &grayscale, &inv_grayscale, &biomes, &cold, &warm, &quantized, &pattern };

        auto min_opt = std::make_shared<ptr_option<float>>(0.f, 16.f, 0.1f, 0.f, &_min, "Min range in meters");
        register_option(RS2_OPTION_MIN_DISTANCE, min_opt);

        auto max_opt = std::make_shared<ptr_option<float>>(0.f, 16.f, 0.1f, 6.f, &_max, "Max range in meters");
        register_option(RS2_OPTION_MAX_DISTANCE, max_opt);

        auto color_map = std::make_shared<ptr_option<int>>(0, (int)_maps.size() - 1, 1, 0, &_map_index, "Color map");
		color_map->set_description(0.f, "Jet");
		color_map->set_description(1.f, "Hue");
		color_map->set_description(2.f, "Classic");
        color_map->set_description(3.f, "White to Black");
        color_map->set_description(4.f, "Black to White");
        color_map->set_description(5.f, "Bio");
        color_map->set_description(6.f, "Cold");
        color_map->set_description(7.f, "Warm");
        color_map->set_description(8.f, "Quantized");
        color_map->set_description(9.f, "Pattern");
        register_option(RS2_OPTION_COLOR_SCHEME, color_map);

        auto preset_opt = std::make_shared<ptr_option<int>>(0, 3, 1, 0, &_preset, "Preset depth colorization");
        preset_opt->set_description(0.f, "Dynamic");
        preset_opt->set_description(1.f, "Fixed");
        preset_opt->set_description(2.f, "Near");
        preset_opt->set_description(3.f, "Far");

        preset_opt->on_set([this](float val)
        {
            if (fabs(val - 0.f) < 1e-6)
            {
                // Dynamic
                _equalize = true;
                _map_index = 0;
            }
            if (fabs(val - 1.f) < 1e-6)
            {
                // Fixed
                _equalize = false;
                _map_index = 0;
                _min = 0.f;
                _max = 6.f;
            }
            if (fabs(val - 2.f) < 1e-6)
            {
                // Near
                _equalize = false;
                _map_index = 1;
                _min = 0.3f;
                _max = 1.5f;
            }
            if (fabs(val - 3.f) < 1e-6)
            {
                // Far
                _equalize = false;
                _map_index = 0;
                _min = 1.f;
                _max = 16.f;
            }
        });
        register_option(RS2_OPTION_VISUAL_PRESET, preset_opt);

        auto hist_opt = std::make_shared<ptr_option<bool>>(false, true, true, true, &_equalize, "Perform histogram equalization");
        register_option(RS2_OPTION_HISTOGRAM_EQUALIZATION_ENABLED, hist_opt);
    }

	bool colorizer::should_process(const rs2::frame& frame)
	{
		if (!frame || frame.is<rs2::frameset>())
			return false;

		if (frame.get_profile().stream_type() != RS2_STREAM_DEPTH)
			return false;

		return true;
	}

    rs2::frame colorizer::process_frame(const rs2::frame_source& source, const rs2::frame& f)
    {
        if (f.get_profile().get() != _source_stream_profile.get())
        {
            _source_stream_profile = f.get_profile();
            _target_stream_profile = f.get_profile().clone(RS2_STREAM_DEPTH, 0, RS2_FORMAT_RGB8);

			// set params for handling disparity
			if (_source_stream_profile.format() == RS2_FORMAT_DISPARITY32)
			{
				const uint8_t fractional_bits = 5;
				const uint8_t fractions = 1 << fractional_bits;
				_d2d_convert_factor = (_stereo_baseline_meter * _focal_lenght_mm * fractions) / _depth_units;

				// Check if the new frame originated from stereo-based depth sensor
				// and retrieve the stereo baseline parameter that will be used in transformations
				auto snr = ((frame_interface*)f.get())->get_sensor().get();
				librealsense::depth_stereo_sensor* dss;

				// Playback sensor
				if (auto a = As<librealsense::extendable_interface>(snr))
				{
					librealsense::depth_stereo_sensor* ptr;
					if (_stereoscopic_depth = a->extend_to(TypeToExtension<librealsense::depth_stereo_sensor>::value, (void**)&ptr))
					{
						dss = ptr;
						_depth_units = dss->get_depth_scale();
						_stereo_baseline_meter = dss->get_stereo_baseline_mm()*0.001f;
					}
				}
				else // Live sensor
				{
					_stereoscopic_depth = Is<librealsense::depth_stereo_sensor>(snr);
					if (_stereoscopic_depth)
					{
						dss = As<librealsense::depth_stereo_sensor>(snr);
						_depth_units = dss->get_depth_scale();
						_stereo_baseline_meter = dss->get_stereo_baseline_mm()* 0.001f;
					}
				}

				if (_stereoscopic_depth)
				{
					auto vp = _source_stream_profile.as<rs2::video_stream_profile>();
					_focal_lenght_mm = vp.get_intrinsics().fx;
					const uint8_t fractional_bits = 5;
					const uint8_t fractions = 1 << fractional_bits;
					_d2d_convert_factor = (_stereo_baseline_meter * _focal_lenght_mm * fractions) / _depth_units;
				}
			}
        }

        auto make_equalized_histogram = [this](const rs2::video_frame& depth, rs2::video_frame rgb)
        {
            const auto w = depth.get_width(), h = depth.get_height();
            const auto depth_data = reinterpret_cast<const uint16_t*>(depth.get_data());
            auto rgb_data = reinterpret_cast<uint8_t*>(const_cast<void *>(rgb.get_data()));

            update_histogram(_hist_data, depth_data, w, h);
            
            auto cm = _maps[_map_index];
            for (auto i = 0; i < w*h; ++i)
            {
                auto d = depth_data[i];

                if (d)
                {
                    auto f = _hist_data[d] / (float)_hist_data[MAX_DEPTH-1]; // 0-255 based on histogram location

                    auto c = cm->get(f);
                    rgb_data[i * 3 + 0] = (uint8_t)c.x;
                    rgb_data[i * 3 + 1] = (uint8_t)c.y;
                    rgb_data[i * 3 + 2] = (uint8_t)c.z;
                }
                else
                {
                    rgb_data[i * 3 + 0] = 0;
                    rgb_data[i * 3 + 1] = 0;
                    rgb_data[i * 3 + 2] = 0;
                }
            }
        };

        auto make_value_cropped_frame = [this](const rs2::video_frame& depth, rs2::video_frame rgb)
        {
            const auto w = depth.get_width(), h = depth.get_height();
            const auto depth_data = reinterpret_cast<const uint16_t*>(depth.get_data());
            auto rgb_data = reinterpret_cast<uint8_t*>(const_cast<void *>(rgb.get_data()));

            auto fi = (frame_interface*)depth.get();
            auto df = dynamic_cast<librealsense::depth_frame*>(fi);
            auto depth_units = df->get_units();

            for (auto i = 0; i < w*h; ++i)
            {
                auto d = depth_data[i];

                if (d)
                {
                    auto f = (d * depth_units - _min) / (_max - _min);

                    auto c = _maps[_map_index]->get(f);
                    rgb_data[i * 3 + 0] = (uint8_t)c.x;
                    rgb_data[i * 3 + 1] = (uint8_t)c.y;
                    rgb_data[i * 3 + 2] = (uint8_t)c.z;
                }
                else
                {
                    rgb_data[i * 3 + 0] = 0;
                    rgb_data[i * 3 + 1] = 0;
                    rgb_data[i * 3 + 2] = 0;
                }
            }
        };

		// for disparity colorization with equalization
		auto make_disparity_equalized_histogram = [this](const rs2::video_frame& depth, rs2::video_frame rgb)
		{
			const auto w = depth.get_width(), h = depth.get_height();
			const auto disparity_data = reinterpret_cast<const float*>(depth.get_data());
			auto rgb_data = reinterpret_cast<uint8_t*>(const_cast<void *>(rgb.get_data()));

			update_disparity_histogram(_hist_data, disparity_data, w, h);

			auto cm = _maps[_map_index];
			for (auto i = 0; i < w*h; ++i)
			{
				auto d = (int)disparity_data[i];

				if (d)
				{
					auto f = _hist_data[d] / (float)_hist_data[MAX_DEPTH - 1]; // 0-255 based on histogram location

					auto c = cm->get(f);
					rgb_data[i * 3 + 0] = (uint8_t)c.x;
					rgb_data[i * 3 + 1] = (uint8_t)c.y;
					rgb_data[i * 3 + 2] = (uint8_t)c.z;
				}
				else
				{
					rgb_data[i * 3 + 0] = 0;
					rgb_data[i * 3 + 1] = 0;
					rgb_data[i * 3 + 2] = 0;
				}
			}
		};

		// for disparity colorization with fixed range
		auto make_disparity_value_cropped_frame = [this](const rs2::video_frame& depth, rs2::video_frame rgb)
		{
			const auto w = depth.get_width(), h = depth.get_height();
			const auto disparity_data = reinterpret_cast<const float*>(depth.get_data());
			auto rgb_data = reinterpret_cast<uint8_t*>(const_cast<void *>(rgb.get_data()));

			auto fi = (frame_interface*)depth.get();
			auto df = dynamic_cast<librealsense::depth_frame*>(fi);
			auto depth_units = df->get_units();

			// convert from depth min max to disparity min max
			// note: max min value is inverted in disparity domain
			auto _disparity_max = static_cast<float>((_d2d_convert_factor / _min) * depth_units + .5f);
			auto _disparity_min = static_cast<float>((_d2d_convert_factor / _max) * depth_units + .5f);

			for (auto i = 0; i < w*h; ++i)
			{
				auto d = disparity_data[i];

				if (d)
				{
					// colorize with disparity
					auto f = (d - _disparity_min) / (_disparity_max - _disparity_min);
					auto c = _maps[_map_index]->get(f);
					rgb_data[i * 3 + 0] = (uint8_t)c.x;
					rgb_data[i * 3 + 1] = (uint8_t)c.y;
					rgb_data[i * 3 + 2] = (uint8_t)c.z;
				}
				else
				{
					rgb_data[i * 3 + 0] = 0;
					rgb_data[i * 3 + 1] = 0;
					rgb_data[i * 3 + 2] = 0;
				}
			}
		};

        rs2::frame ret;

        auto vf = f.as<rs2::video_frame>();
        //rs2_extension ext = f.is<rs2::disparity_frame>() ? RS2_EXTENSION_DISPARITY_FRAME : RS2_EXTENSION_DEPTH_FRAME;
        ret = source.allocate_video_frame(_target_stream_profile, f, 3, vf.get_width(), vf.get_height(), vf.get_width() * 3, RS2_EXTENSION_VIDEO_FRAME);

		if (_source_stream_profile.format() == RS2_FORMAT_DISPARITY32)
		{
			if (_equalize)
				make_disparity_equalized_histogram(f, ret);
			else
				make_disparity_value_cropped_frame(f, ret);
		}
		else
		{ 
			if (_equalize)
				make_equalized_histogram(f, ret);
			else
				make_value_cropped_frame(f, ret);
		}

        return ret;
    }
}
