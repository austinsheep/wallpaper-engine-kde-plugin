#include "Device.hpp"

#include "Utils/Logging.h"

using namespace wallpaper::vulkan;

vk::Result CreateDevice(Instance& instance,
	Span<vk::DeviceQueueCreateInfo> queueCreateInfos,
	Span<const char*const> deviceExts,
	vk::Device* device) {
	
	vk::DeviceCreateInfo deviceCreateInfo;
	deviceCreateInfo
		.setQueueCreateInfoCount(queueCreateInfos.size())
		.setPQueueCreateInfos(queueCreateInfos.data())
		.setEnabledExtensionCount(deviceExts.size())
		.setPpEnabledExtensionNames(deviceExts.data());

    return instance.gpu().createDevice(&deviceCreateInfo, nullptr, device); 
}


std::vector<vk::DeviceQueueCreateInfo> Device::ChooseDeviceQueue(bool present) {
	std::vector<vk::DeviceQueueCreateInfo> queues;
	auto props = m_gpu.getQueueFamilyProperties();
	std::vector<uint32_t> graphic_indexs, present_indexs;
	uint32_t index = 0;
	for(auto& prop:props) {
		if(prop.queueFlags & vk::QueueFlagBits::eGraphics)
			graphic_indexs.push_back(index);
		index++;
	};
	m_graphics_queue.family_index = graphic_indexs.front();
	const static float defaultQueuePriority = 0.0f;
	{
		vk::DeviceQueueCreateInfo info;
		info
			.setQueueCount(1)
			.setQueueFamilyIndex(m_graphics_queue.family_index)
			.setPQueuePriorities(&defaultQueuePriority);
		queues.push_back(info);
	}
	if(present) {
		index = 0;
		for(auto& prop:props) {
			/*
			if(m_gpu.getSurfaceSupportKHR(index, vct.surface))
				present_indexs.push_back(index);
			index++;
			*/
		};
		m_present_queue.family_index = graphic_indexs.front();
		if(graphic_indexs.front() != present_indexs.front()) {
			m_present_queue.family_index = present_indexs.front();
			vk::DeviceQueueCreateInfo info;
			info
				.setQueueCount(1)
				.setQueueFamilyIndex(m_present_queue.family_index)
				.setPQueuePriorities(&defaultQueuePriority);
			queues.push_back(info);
		}
	}
	return queues;
}

vk::ResultValue<Device> Device::Create(Instance& inst, Span<const char*const> exts) {
	vk::ResultValue<Device> rv {vk::Result::eIncomplete, {}};
	auto& device = rv.value;
	device.m_gpu = inst.gpu();
	do {
		rv.result = CreateDevice(inst, device.ChooseDeviceQueue(false), exts, &device.m_device);
		if(rv.result != vk::Result::eSuccess) break;
		device.m_graphics_queue.handle = device.m_device.getQueue(device.m_graphics_queue.family_index, 0);

		if(inst.surface()) {
			rv.result = Swapchain::Create(device, inst.surface(), {1280, 720}, device.m_swapchain);
			if(rv.result != vk::Result::eSuccess) {
				LOG_ERROR("create swapchain failed");
				break;
			}
		}

	  	auto rv_cmdpool = device.m_device.createCommandPool({
			vk::CommandPoolCreateFlagBits::eTransient|
			vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			device.m_graphics_queue.family_index});
		if(rv_cmdpool.result != vk::Result::eSuccess) { rv.result = rv_cmdpool.result; break; }
		device.m_command_pool = rv_cmdpool.value;
		{
			VmaAllocatorCreateInfo allocatorInfo = {};
			allocatorInfo.physicalDevice = device.m_gpu;
			allocatorInfo.device = device.m_device;
			allocatorInfo.instance = inst.inst();
			vmaCreateAllocator(&allocatorInfo, &device.m_allocator);
		}
		rv.result = vk::Result::eSuccess;
	} while(false);
	return rv;
}

void Device::Destroy() {
	if(m_device) {
		(void)m_device.waitIdle();
		m_device.destroyCommandPool(m_command_pool);
		vmaDestroyAllocator(m_allocator);
		m_device.destroy();
	}
}

vk::Result Device::CreateRenderingResource(RenderingResources& rr) {
	vk::Result result;
	do {
		auto rv_cmd = m_device.allocateCommandBuffers({m_command_pool, vk::CommandBufferLevel::ePrimary, 1});
		result = rv_cmd.result;
		if(result != vk::Result::eSuccess) break;
		rr.command = rv_cmd.value.front();

		auto rv_f = m_device.createFence({vk::FenceCreateFlagBits::eSignaled});
		result = rv_f.result;
		if(result != vk::Result::eSuccess) break;
		rr.fence_frame = rv_f.value;
		m_device.resetFences(1, &rr.fence_frame);
	} while(false);
	return result;
}


void Device::DestroyRenderingResource(RenderingResources& rr) {
	m_device.freeCommandBuffers(m_command_pool, 1, &rr.command);
	m_device.destroyFence(rr.fence_frame);
}