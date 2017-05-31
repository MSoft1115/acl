#pragma once

////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2017 Nicholas Frechette & Animation Compression Library contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
////////////////////////////////////////////////////////////////////////////////

#include "acl/compressed_clip.h"
#include "acl/core/utils.h"
#include "acl/math/quat_32.h"
#include "acl/math/vector4_32.h"
#include "acl/algorithm/full_precision_common.h"
#include "acl/decompression/output_writer.h"

#include <stdint.h>

//////////////////////////////////////////////////////////////////////////
// See encoder for details
//////////////////////////////////////////////////////////////////////////

namespace acl
{
	// 2 ways to encore a track as default: a bitset or omit the track
	// the second method requires a track id to be present to distinguish the
	// remaining tracks.
	// For a character, about 50-90 tracks are animated.
	// We ideally want to support more than 255 tracks or bones.
	// 50 * 16 bits = 100 bytes
	// 90 * 16 bits = 180 bytes
	// On the other hand, a character has about 140-180 bones, or 280-360 tracks (rotation/translation only)
	// 280 * 1 bit = 35 bytes
	// 360 * 1 bit = 45 bytes
	// It is obvious that storing a bitset is much more compact
	// A bitset also allows us to process and write track values in the order defined when compressed
	// unlike the track id method which makes it impossible to know which values are default until
	// everything has been decompressed (at which point everything else is default).
	// For the track id method to be more compact, an unreasonable small number of tracks would need to be
	// animated or constant compared to the total possible number of tracks. Those are likely to be rare.

	template<class OutputWriterType>
	inline void full_precision_decoder(const CompressedClip& clip, float sample_time, OutputWriterType& writer)
	{
		ensure(clip.get_algorithm_type() == AlgorithmType::FullPrecision);
		ensure(clip.is_valid(false));

		const FullPrecisionHeader& header = get_full_precision_header(clip);

		float clip_duration = float(header.num_samples - 1) / float(header.sample_rate);

		uint32_t key_frame0;
		uint32_t key_frame1;
		float interpolation_alpha;
		calculate_interpolation_keys(header.num_samples, clip_duration, sample_time, key_frame0, key_frame1, interpolation_alpha);

		uint32_t bitset_size = ((header.num_bones * FullPrecisionConstants::NUM_TRACKS_PER_BONE) + FullPrecisionConstants::BITSET_WIDTH - 1) / FullPrecisionConstants::BITSET_WIDTH;

		const uint32_t* default_tracks_bitset = header.get_default_tracks_bitset();
		uint32_t default_track_offset = 0;

		const uint32_t* constant_tracks_bitset = header.get_constant_tracks_bitset();
		uint32_t constant_track_offset = 0;

		const float* constant_track_data = header.get_constant_track_data();

		// TODO: No need to store this, unpack from bitset in context and simplify branching logic below?
		uint32_t num_animated_floats_per_key_frame = (header.num_animated_rotation_tracks * 4) + (header.num_animated_translation_tracks * 3);

		const float* animated_track_data = header.get_track_data();
		const float* key_frame_data0 = animated_track_data + (key_frame0 * num_animated_floats_per_key_frame);
		const float* key_frame_data1 = animated_track_data + (key_frame1 * num_animated_floats_per_key_frame);

		for (uint32_t bone_index = 0; bone_index < header.num_bones; ++bone_index)
		{
			Quat_32 rotation;
			bool is_rotation_default = bitset_test(default_tracks_bitset, bitset_size, default_track_offset);
			if (is_rotation_default)
			{
				rotation = quat_32_identity();
			}
			else
			{
				bool is_rotation_constant = bitset_test(constant_tracks_bitset, bitset_size, constant_track_offset);
				if (is_rotation_constant)
				{
					rotation = quat_unaligned_load(constant_track_data);
					constant_track_data += 4;
				}
				else
				{
					Quat_32 rotation0 = quat_unaligned_load(key_frame_data0);
					Quat_32 rotation1 = quat_unaligned_load(key_frame_data1);
					rotation = quat_lerp(rotation0, rotation1, interpolation_alpha);

					key_frame_data0 += 4;
					key_frame_data1 += 4;
				}
			}

			default_track_offset++;
			constant_track_offset++;

			writer.write_bone_rotation(bone_index, rotation);

			Vector4_32 translation;
			bool is_translation_default = bitset_test(default_tracks_bitset, bitset_size, default_track_offset);
			if (is_translation_default)
			{
				translation = vector_32_zero();
			}
			else
			{
				bool is_translation_constant = bitset_test(constant_tracks_bitset, bitset_size, constant_track_offset);
				if (is_translation_constant)
				{
					translation = vector_unaligned_load3(constant_track_data);
					constant_track_data += 3;
				}
				else
				{
					Vector4_32 translation0 = vector_unaligned_load3(key_frame_data0);
					Vector4_32 translation1 = vector_unaligned_load3(key_frame_data1);
					translation = vector_lerp(translation0, translation1, interpolation_alpha);

					key_frame_data0 += 3;
					key_frame_data1 += 3;
				}
			}

			default_track_offset++;
			constant_track_offset++;

			writer.write_bone_translation(bone_index, translation);
		}
	}
}