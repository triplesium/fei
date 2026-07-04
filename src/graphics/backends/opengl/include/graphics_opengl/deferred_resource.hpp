#pragma once

namespace fei {

class DeferredResourceOpenGL {
  public:
    virtual ~DeferredResourceOpenGL() = default;

    bool created() const noexcept { return m_created; }

    void ensure_created() const {
        if (m_created) {
            return;
        }

        create_gl_resource();
        m_created = true;
    }

    void dispose() {
        if (!m_created) {
            return;
        }

        destroy_gl_resource();
        m_created = false;
    }

  protected:
    DeferredResourceOpenGL() = default;
    explicit DeferredResourceOpenGL(bool created) : m_created(created) {}

  private:
    virtual void create_gl_resource() const = 0;
    virtual void destroy_gl_resource() = 0;

    mutable bool m_created {false};
};

} // namespace fei
