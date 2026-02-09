#pragma once
#include "../../Core/Public/Expected.hpp"
#include "Renderable.hpp"

#include <cstdint>
#include <vector>

#include <DirectXMath.h>

struct MeshData;    // Forward declare — full definition in Assets/Public/MeshData.hpp
struct TextureData; // Forward declare — full definition in Assets/Public/TextureData.hpp

enum class KHR_Settings;

NOC_SUPPRESS_DLL_WARNINGS

/// Abstract renderer interface. No graphics-API types leak through this surface.
/// Concrete implementations (e.g. Vulkan) inherit from this.
class NOC_EXPORT IRenderer
{
  public:
    virtual ~IRenderer() = default;

    /// Initialize the rendering backend. Must be called before any other method.
    virtual Result<> initialize() noexcept = 0;

    /// Render one frame using the current view/projection and renderables.
    virtual Result<> draw_frame() noexcept = 0;

    /// Block until all GPU work is finished. Call before shutdown.
    virtual void wait_idle() noexcept = 0;

    /// Upload CPU-side mesh data to GPU buffers. Returns an opaque mesh index.
    /// The MeshData must have been loaded via AssetManager beforehand.
    virtual Result<uint32_t> upload_mesh(const MeshData& meshData) = 0;

    /// Upload CPU-side texture data to GPU image. Returns an opaque texture index.
    /// The TextureData must have been loaded via AssetManager beforehand.
    virtual Result<uint32_t> upload_texture(const TextureData& textureData) = 0;

    /// Create a GPU material from texture indices. Returns an opaque material index.
    /// albedoTextureIndex and normalTextureIndex must be valid indices from upload_texture().
    virtual Result<uint32_t> upload_material(uint32_t albedoTextureIndex, uint32_t normalTextureIndex) = 0;

    /// Set the view and projection matrices for this frame.
    virtual void set_view_projection(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj) noexcept = 0;

    /// Set the list of renderables to draw this frame.
    virtual void set_renderables(std::vector<Renderable> renderables) noexcept = 0;

    /// Notify the renderer that the window framebuffer was resized.
    virtual void on_framebuffer_resized() noexcept = 0;

    /// Returns the current render target width in pixels.
    virtual uint32_t get_render_width() const noexcept = 0;

    /// Returns the current render target height in pixels.
    virtual uint32_t get_render_height() const noexcept = 0;

    // --- Runtime settings ---

    /// Set VSync mode. true = FIFO (vsync on), false = IMMEDIATE or MAILBOX.
    virtual void set_vsync(KHR_Settings mode) noexcept = 0;

    /// Get current VSync state.
    virtual bool get_vsync() const noexcept = 0;

    /// Set MSAA sample count (1, 2, 4, 8). Clamped to device max.
    virtual void set_msaa_samples(int samples) noexcept = 0;

    /// Get current MSAA sample count.
    virtual int get_msaa_samples() const noexcept = 0;

    /// Set render resolution scale (0.25 to 2.0). 1.0 = native resolution.
    virtual void set_render_scale(float scale) noexcept = 0;

    /// Get current render resolution scale.
    virtual float get_render_scale() const noexcept = 0;
};

NOC_RESTORE_DLL_WARNINGS
