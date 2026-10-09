#pragma once

#include <array>
#include <mutex>

#include "dxvk_buffer.h"
#include "dxvk_image.h"
#include "dxvk_sampler.h"

namespace dxvk {

  class DxvkContext;
  
  /**
   * \brief Unbound resources
   * 
   * Creates dummy resources that will be used
   * for descriptor sets when the client API did
   * not bind a compatible resource to a slot.
   */
  class DxvkUnboundResources {
    
  public:
    
    DxvkUnboundResources(DxvkDevice* dev);
    ~DxvkUnboundResources();
    
    /**
     * \brief Dummy buffer handle
     * 
     * Returns a handle to a buffer filled with zeroes.
     * Use for unbound transform feedback buffers only.
     * \returns Dummy buffer handle
     */
    DxvkResourceBufferInfo bufferInfo();

    /**
     * \brief Dummy sampler object
     * 
     * Points to a sampler which was created with
     * reasonable default values. Client APIs may
     * still require different behaviour.
     * \returns Dummy sampler
     */
    DxvkSamplerDescriptor samplerInfo();

    /**
     * \brief Whether unbound slots need real substitute resources
     *
     * kisakcod fork: true when the device lacks the robustness2
     * nullDescriptor feature (MoltenVK) and descriptors are written
     * through legacy descriptor sets, where VK_NULL_HANDLE would be
     * invalid usage.
     */
    bool needsSubstitutes() const;

    /**
     * \brief Substitute sampled image descriptor
     *
     * A 1x1 (or 1x1x1) black RGBA8 image of the requested view type.
     * Only valid after initSubstitutes has run on some context.
     * \returns Legacy descriptor, or nullptr for unsupported types
     */
    const DxvkLegacyDescriptor* imageSubstitute(VkImageViewType viewType);

    /**
     * \brief Substitute uniform or storage buffer descriptor
     * \returns Legacy descriptor for the zero-filled dummy buffer
     */
    DxvkLegacyDescriptor bufferSubstitute();

    /**
     * \brief Substitute texel buffer view descriptor
     * \returns Legacy descriptor for an R32_UINT view of the dummy buffer
     */
    DxvkLegacyDescriptor texelBufferSubstitute(bool storage);

    /**
     * \brief Creates and clears the substitute images
     *
     * Must be called outside of a render pass. Only the first
     * call does any work.
     */
    void initSubstitutes(DxvkContext* ctx);

  private:
    
    DxvkDevice*             m_device;

    std::atomic<bool>       m_bufferCreated = { false };
    std::atomic<bool>       m_samplerCreated = { false };

    dxvk::mutex             m_mutex;
    Rc<DxvkSampler>         m_sampler;
    Rc<DxvkBuffer>          m_buffer;
    
    std::atomic<bool>       m_substitutesReady = { false };
    std::array<Rc<DxvkImage>, 3> m_substituteImages;
    std::array<Rc<DxvkImageView>, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY + 1> m_substituteViews;
    Rc<DxvkBufferView>      m_substituteTexelViews[2];

    Rc<DxvkSampler> createSampler();
    
    Rc<DxvkBuffer> createBuffer();
    
  };
  
}
