//----------------------------------------------------------------------------//
//                                                                            //
// ozz-animation is hosted at http://github.com/guillaumeblanc/ozz-animation  //
// and distributed under the MIT License (MIT).                               //
//                                                                            //
// Copyright (c) 2017 Guillaume Blanc                                         //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included in //
// all copies or substantial portions of the Software.                        //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//----------------------------------------------------------------------------//
#include <experimental/filesystem>

#include "animation/offline/fbx/fbx2ozz.h"

#include "ozz/animation/offline/fbx/fbx_animation.h"

#include "ozz/animation/offline/raw_animation.h"

#include "ozz/base/log.h"

#include "ozz/animation/offline/animation_builder.h"

#include "ozz/base/io/stream.h"
#include "ozz/base/io/archive.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/offline/animation_optimizer.h"

Fbx2OzzImporter::AnimationNames Fbx2OzzImporter::GetAnimationNames() {
  if (!scene_loader_) {
    return AnimationNames();
  }
  return ozz::animation::offline::fbx::GetAnimationNames(*scene_loader_);
}

bool Fbx2OzzImporter::Import(
    const char* _animation_name, const ozz::animation::Skeleton& _skeleton,
    float _sampling_rate, ozz::animation::offline::RawAnimation* _animation) {
  if (!_animation) {
    return false;
  }

  *_animation = ozz::animation::offline::RawAnimation();
  if (!scene_loader_) {
    return false;
  }
  
  auto ret =  ozz::animation::offline::fbx::ExtractAnimation(
      _animation_name, *scene_loader_, _skeleton, _sampling_rate, _animation, xAxisFlip);
  if (ret)
  {
	  FbxTime::EMode mode = scene_loader_->scene()->GetGlobalSettings().GetTimeMode();
	  const float scene_frame_rate =
		  static_cast<float>((mode == FbxTime::eCustom)
			  ? scene_loader_->scene()->GetGlobalSettings().GetCustomFrameRate()
			  : FbxTime::GetFrameRate(mode));
	  for (auto& desc : this->splitAnimDescs)
	  {
		  desc.startTime = desc.startFrame / scene_frame_rate;
		  desc.endTime = desc.endFrame / scene_frame_rate;
	  }
  }
  return ret;
}

void Fbx2OzzImporter::OnRawAnimationGenerated(ozz::animation::offline::AnimationOptimizer& optimizer, const ozz::animation::Skeleton& skeleton, ozz::animation::offline::RawAnimation& _animation)
{
	if (!std::experimental::filesystem::is_directory("clips"))
	{
		std::experimental::filesystem::create_directory("clips");
	}
	for (auto& desc : this->splitAnimDescs)
	{
		auto* splited = ozz::memory::default_allocator()->New<ozz::animation::offline::RawAnimation>();
		splited->name = desc.name.c_str();
		splited->tracks.resize(_animation.tracks.size());
		splited->duration = desc.endTime - desc.startTime;

		for (size_t i = 0; i < _animation.tracks.size(); i++)
		{
			auto& track = splited->tracks[i];
			const auto& trackSrc = _animation.tracks[i];
			for (auto& src : trackSrc.translations)
			{
				if (src.time > desc.endTime)
				{
					break;
				}
				if (src.time >= desc.startTime)
				{
					auto copied = src;
					copied.time -= desc.startTime;
					track.translations.push_back(copied);
				}
			}
			for (auto& src : trackSrc.rotations)
			{
				if (src.time > desc.endTime)
				{
					break;
				}
				if (src.time >= desc.startTime)
				{
					auto copied = src;
					copied.time -= desc.startTime;
					track.rotations.push_back(copied);
				}
			}
			for (auto& src : trackSrc.scales)
			{
				if (src.time > desc.endTime)
				{
					break;
				}
				if (src.time >= desc.startTime)
				{
					auto copied = src;
					copied.time -= desc.startTime;
					track.scales.push_back(copied);
				}
			}
		}
		ozz::animation::offline::RawAnimation raw_optimized_animation;
		optimizer(*splited, skeleton, &raw_optimized_animation);
		ozz::animation::offline::AnimationBuilder builder;
		auto* anim = builder(raw_optimized_animation);
		std::string filePath;
		filePath.append("clips/");
		filePath.append(desc.name.c_str());
		filePath.append(".bytes");
		ozz::io::File clipFile(filePath.c_str(), "wb");
		ozz::io::OArchive stream(&clipFile);
		stream << *anim;
		ozz::memory::default_allocator()->Delete(anim);
		ozz::memory::default_allocator()->Delete(splited);
	}
}


Fbx2OzzImporter::NodeProperties Fbx2OzzImporter::GetNodeProperties(
    const char* _node_name) {
  if (!scene_loader_) {
    return NodeProperties();
  }

  return ozz::animation::offline::fbx::GetNodeProperties(*scene_loader_,
                                                         _node_name);
}

template <typename _RawTrack>
bool ImportImpl(ozz::animation::offline::fbx::FbxSceneLoader* _scene_loader,
                const char* _animation_name, const char* _node_name,
                const char* _track_name, float _sampling_rate,
                _RawTrack* _track) {
  if (!_scene_loader) {
    return false;
  }
  return ozz::animation::offline::fbx::ExtractTrack(_animation_name, _node_name,
                                                    _track_name, *_scene_loader,
                                                    _sampling_rate, _track);
}

bool Fbx2OzzImporter::Import(const char* _animation_name,
                             const char* _node_name, const char* _track_name,
                             float _sampling_rate,
                             ozz::animation::offline::RawFloatTrack* _track) {
  return ImportImpl(scene_loader_, _animation_name, _node_name, _track_name,
                    _sampling_rate, _track);
}

bool Fbx2OzzImporter::Import(const char* _animation_name,
                             const char* _node_name, const char* _track_name,
                             float _sampling_rate,
                             ozz::animation::offline::RawFloat2Track* _track) {
  return ImportImpl(scene_loader_, _animation_name, _node_name, _track_name,
                    _sampling_rate, _track);
}

bool Fbx2OzzImporter::Import(const char* _animation_name,
                             const char* _node_name, const char* _track_name,
                             float _sampling_rate,
                             ozz::animation::offline::RawFloat3Track* _track) {
  return ImportImpl(scene_loader_, _animation_name, _node_name, _track_name,
                    _sampling_rate, _track);
}

bool Fbx2OzzImporter::Import(const char* _animation_name,
                             const char* _node_name, const char* _track_name,
                             float _sampling_rate,
                             ozz::animation::offline::RawFloat4Track* _track) {
  return ImportImpl(scene_loader_, _animation_name, _node_name, _track_name,
                    _sampling_rate, _track);
}
