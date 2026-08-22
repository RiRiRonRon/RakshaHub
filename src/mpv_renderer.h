#pragma once

#include <QQuickFramebufferObject>
#include <QOpenGLFramebufferObject>

class MpvObject;


class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer(MpvObject *obj);
    ~MpvRenderer() override;
    void render() override;
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override;

private:
    MpvObject *m_obj         = nullptr;
    bool       m_initialized = false;
};