#include "mpv_renderer.h"
#include "mpv_object.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickWindow>



static void *getProcAddress(void * /*ctx*/, const char *name)
{
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx) return nullptr;
    return reinterpret_cast<void *>(glctx->getProcAddress(name));
}


MpvRenderer::MpvRenderer(MpvObject *obj)
    : m_obj(obj)
{
}


MpvRenderer::~MpvRenderer()
{

}

QOpenGLFramebufferObject *MpvRenderer::createFramebufferObject(const QSize &size)
{

    if (!m_initialized) {
        m_initialized = true;

        mpv_opengl_init_params glInitParams{};
        glInitParams.get_proc_address = getProcAddress;
        glInitParams.get_proc_address_ctx = nullptr;

        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_API_TYPE,
             const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL) },
            { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams },
            { MPV_RENDER_PARAM_INVALID, nullptr }
        };

        mpv_render_context *ctx = nullptr;
        int r = mpv_render_context_create(&ctx, m_obj->mpvHandle(), params);
        if (r < 0) {
            qWarning("MpvRenderer: failed to create render context: %s",
                     mpv_error_string(r));
        } else {

            mpv_render_context_set_update_callback(
                ctx, MpvObject::mpvUpdateCallback, m_obj);

            m_obj->setRenderContext(ctx);
        }
    }


    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    return new QOpenGLFramebufferObject(size, fmt);
}

void MpvRenderer::render()
{
    mpv_render_context *ctx = m_obj->renderContext();
    if (!ctx) return;

    QOpenGLFramebufferObject *fbo = framebufferObject();
    QQuickWindow *win = m_obj->window();


    win->beginExternalCommands();

    mpv_opengl_fbo mpvFbo{};
    mpvFbo.fbo    = static_cast<int>(fbo->handle());
    mpvFbo.w      = fbo->width();
    mpvFbo.h      = fbo->height();
    mpvFbo.internal_format = 0;
    int flipY =1;

    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo },
        { MPV_RENDER_PARAM_FLIP_Y,     &flipY  },
        { MPV_RENDER_PARAM_INVALID,    nullptr }
    };

    mpv_render_context_render(ctx, params);

    win->endExternalCommands();
}