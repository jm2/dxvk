#include "dxvk_context.h"
#include "dxvk_device.h"

namespace dxvk {
  
  DxvkUnboundResources::DxvkUnboundResources(DxvkDevice* dev)
  : m_device(dev) {

  }
  
  
  DxvkUnboundResources::~DxvkUnboundResources() {
    
  }


  DxvkResourceBufferInfo DxvkUnboundResources::bufferInfo() {
    if (unlikely(!m_bufferCreated.load())) {
      std::lock_guard lock(m_mutex);

      if (!m_bufferCreated.load()) {
        m_buffer = createBuffer();
        m_bufferCreated.store(true);
      }
    }

    return m_buffer->getSliceInfo();;
  }


  DxvkSamplerDescriptor DxvkUnboundResources::samplerInfo() {
    if (unlikely(!m_samplerCreated.load())) {
      std::lock_guard lock(m_mutex);

      if (!m_samplerCreated.load()) {
        m_sampler = createSampler();
        m_samplerCreated.store(true);
      }
    }

    return m_sampler->getDescriptor();
  }


  bool DxvkUnboundResources::needsSubstitutes() const {
    return !m_device->features().extRobustness2.nullDescriptor
        && !m_device->canUseDescriptorHeap()
        && !m_device->canUseDescriptorBuffer();
  }


  const DxvkLegacyDescriptor* DxvkUnboundResources::imageSubstitute(VkImageViewType viewType) {
    if (!m_substitutesReady.load())
      return nullptr;

    // Bindings that accept any view type (VK_IMAGE_VIEW_TYPE_MAX_ENUM, as
    // D3D9 samplers declare) get the 2D substitute.
    if (uint32_t(viewType) >= m_substituteViews.size())
      viewType = VK_IMAGE_VIEW_TYPE_2D;

    const auto& view = m_substituteViews[viewType];
    const DxvkDescriptor* descriptor = view ? view->getDescriptor(viewType) : nullptr;
    return descriptor ? &descriptor->legacy : nullptr;
  }


  DxvkLegacyDescriptor DxvkUnboundResources::bufferSubstitute() {
    DxvkResourceBufferInfo slice = bufferInfo();

    DxvkLegacyDescriptor result = { };
    result.buffer.buffer = slice.buffer;
    result.buffer.offset = slice.offset;
    result.buffer.range = slice.size;
    return result;
  }


  DxvkLegacyDescriptor DxvkUnboundResources::texelBufferSubstitute(bool storage) {
    bufferInfo();

    std::lock_guard lock(m_mutex);
    auto& view = m_substituteTexelViews[storage ? 1u : 0u];

    if (!view) {
      DxvkBufferViewKey key = { };
      key.format = VK_FORMAT_R32_UINT;
      key.usage = storage
        ? VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
        : VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
      key.offset = 0u;
      key.size = m_buffer->info().size;
      view = m_buffer->createView(key);
    }

    return view->getDescriptor(false)->legacy;
  }


  void DxvkUnboundResources::initSubstitutes(DxvkContext* ctx) {
    if (likely(m_substitutesReady.load()) || !needsSubstitutes())
      return;

    std::lock_guard lock(m_mutex);

    if (m_substitutesReady.load())
      return;

    // One image per dimensionality; the 2D image has six layers so that
    // cube and cube array views can share it.
    const std::array<VkImageType, 3> types = { VK_IMAGE_TYPE_1D, VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_3D };

    for (uint32_t i = 0u; i < types.size(); i++) {
      DxvkImageCreateInfo info = { };
      info.type = types[i];
      info.format = VK_FORMAT_R8G8B8A8_UNORM;
      info.flags = types[i] == VK_IMAGE_TYPE_2D ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u;
      info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      info.extent = { 1u, 1u, 1u };
      info.numLayers = types[i] == VK_IMAGE_TYPE_2D ? 6u : 1u;
      info.mipLevels = 1u;
      info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | m_device->getShaderPipelineStages();
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
      info.tiling = VK_IMAGE_TILING_OPTIMAL;
      info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      info.debugName = "Null descriptor substitute";

      m_substituteImages[i] = m_device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      ctx->initImage(m_substituteImages[i], VK_IMAGE_LAYOUT_UNDEFINED);
    }

    struct ViewDef { VkImageViewType type; uint32_t image; uint32_t layers; };
    const std::array<ViewDef, 7> views = {{
      { VK_IMAGE_VIEW_TYPE_1D,         0u, 1u },
      { VK_IMAGE_VIEW_TYPE_1D_ARRAY,   0u, 1u },
      { VK_IMAGE_VIEW_TYPE_2D,         1u, 1u },
      { VK_IMAGE_VIEW_TYPE_2D_ARRAY,   1u, 1u },
      { VK_IMAGE_VIEW_TYPE_CUBE,       1u, 6u },
      { VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, 1u, 6u },
      { VK_IMAGE_VIEW_TYPE_3D,         2u, 1u },
    }};

    for (const auto& def : views) {
      if (def.type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY && !m_device->features().core.features.imageCubeArray)
        continue;

      DxvkImageViewKey key = { };
      key.viewType = def.type;
      key.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      key.format = VK_FORMAT_R8G8B8A8_UNORM;
      key.aspects = VK_IMAGE_ASPECT_COLOR_BIT;
      key.mipIndex = 0u;
      key.mipCount = 1u;
      key.layerIndex = 0u;
      key.layerCount = def.layers;

      m_substituteViews[def.type] = m_substituteImages[def.image]->createView(key);
    }

    m_substitutesReady.store(true);
  }


  Rc<DxvkSampler> DxvkUnboundResources::createSampler() {
    DxvkSamplerKey info;
    info.setFilter(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    info.setLodRange(-256.0f, 256.0f, 0.0f);
    info.setAddressModes(
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    info.setReduction(VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE);

    return m_device->createSampler(info);
  }
  
  
  Rc<DxvkBuffer> DxvkUnboundResources::createBuffer() {
    DxvkBufferCreateInfo info;
    info.size       = MaxUniformBufferSize;
    info.usage      = VK_BUFFER_USAGE_TRANSFER_DST_BIT
                    | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                    | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                    | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                    | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
                    | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
    info.stages     = VK_PIPELINE_STAGE_TRANSFER_BIT
                    | m_device->getShaderPipelineStages();
    info.access     = VK_ACCESS_UNIFORM_READ_BIT
                    | VK_ACCESS_SHADER_READ_BIT
                    | VK_ACCESS_SHADER_WRITE_BIT;
    info.debugName  = "Null buffer";
    
    Rc<DxvkBuffer> buffer = m_device->createBuffer(info,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    std::memset(buffer->mapPtr(0), 0, info.size);
    return buffer;
  }
  
}
