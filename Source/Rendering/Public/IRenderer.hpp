#pragma once
#include "../../Core/Public/Expected.hpp"
#include "Renderable.hpp"

#include <cstdint>
#include <vector>

#include <DirectXMath.h>

struct MeshData; // Forward declare — full definition in Assets/Public/MeshData.hpp

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
};

NOC_RESTORE_DLL_WARNINGS
