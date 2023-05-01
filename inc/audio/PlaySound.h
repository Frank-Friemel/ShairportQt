#pragma once

#include <future>
#include <string>
#include <map>
#include <utility>

struct IStream;

namespace AlsaAudio
{
	std::future<int> Play(std::string file_path, std::string device = "default");
	std::future<int> Play(const void* buf, size_t bufsize, std::string device = "default");
	std::future<int> Play(IStream* stream, std::string device = "default");

	std::map<std::string, std::string> ListDevices();
}