#include "fw/Router.hpp"
#include <hv/HttpServer.h>
#include <hv/HttpResponseWriter.h>
#include <utility>  // std::move
#include "fw/WindowsCompat.hpp"  // 必须在 libhv (windows.h) 之后, 清理 DELETE 宏污染

namespace alkaidlab {
namespace fw {

Router::Router() {}

/* -- Middleware -- */

void Router::use(MiddlewareFn fn) { m_middlewares.use(std::move(fn)); }
void Router::use(const std::string& name, MiddlewareFn fn) { m_middlewares.use(name, std::move(fn)); }

/* -- Route registration -- */

void Router::get(const char* path, Handler handler) {
    Route r; r.method = Method::GET; r.path = path; r.handler = std::move(handler); r.async = false;
    m_routes.push_back(std::move(r));
}

void Router::post(const char* path, Handler handler) {
    Route r; r.method = Method::POST; r.path = path; r.handler = std::move(handler); r.async = false;
    m_routes.push_back(std::move(r));
}

void Router::put(const char* path, Handler handler) {
    Route r; r.method = Method::PUT; r.path = path; r.handler = std::move(handler); r.async = false;
    m_routes.push_back(std::move(r));
}

void Router::del(const char* path, Handler handler) {
    Route r; r.method = Method::DELETE; r.path = path; r.handler = std::move(handler); r.async = false;
    m_routes.push_back(std::move(r));
}

void Router::patch(const char* path, Handler handler) {
    Route r; r.method = Method::PATCH; r.path = path; r.handler = std::move(handler); r.async = false;
    m_routes.push_back(std::move(r));
}

void Router::getAsync(const char* path, Handler handler) {
    Route r; r.method = Method::GET; r.path = path; r.handler = std::move(handler); r.async = true;
    m_routes.push_back(std::move(r));
}

void Router::setAsyncDispatcher(std::function<void(std::function<void()>)> dispatcher) {
    m_asyncDispatcher = std::move(dispatcher);
}

void Router::setAsyncTaskTracker(std::function<void(int delta)> tracker) {
    m_asyncTaskTracker = std::move(tracker);
}

void Router::setPostprocessor(std::function<int(Context&)> fn) {
    m_postprocessor = std::move(fn);
}

void Router::setErrorHandler(std::function<int(Context&)> fn) {
    m_errorHandler = std::move(fn);
}

/* -- libhv bridge (internal helpers) -- */

static http_sync_handler
makeSyncHandler(const Handler& handler,
                const std::shared_ptr<MiddlewareChain>& chain) {
    return [chain, handler](HttpRequest* req, HttpResponse* resp) -> int {
        Context ctx(static_cast<void*>(req), static_cast<void*>(resp));
        const Handler& h = handler;
        chain->execute(ctx, [&h](Context& c) -> int {
            h(c);
            return c.status() ? c.status() : 200;
        });
        return ctx.status() ? ctx.status() : 200;
    };
}

static http_ctx_handler
makeAsyncHandler(const Handler& handler,
                 const std::shared_ptr<MiddlewareChain>& chain,
                 const std::function<void(std::function<void()>)>& dispatcher,
                 const std::function<void(int)>& tracker) {
    return [chain, handler, dispatcher, tracker](const HttpContextPtr& httpCtx) -> int {
        httpCtx->request->SetHeader("X-Internal-Async", "1");
        if (tracker) tracker(+1);

        auto task = [chain, handler, httpCtx, tracker]() {
            bool streamingHandoff = false;
            try {
                const HttpContextPtr* ptr = &httpCtx;
                Context ctx(static_cast<const void*>(ptr));
                const Handler& h = handler;
                chain->execute(ctx, [&h](Context& c) -> int {
                    h(c);
                    return c.status() ? c.status() : 200;
                });
                streamingHandoff = ctx.isStreamingHandoff();
            } catch (...) {
                /* Prevent exception from leaking into C event loop */
            }
            /* 流式接管：StreamTransfer 等异步写入器自行负责 End() 的调用时机。
             * 若此处过早调用 End()，对 Connection: close 请求会立即关 socket
             * 导致后续 EPOLLOUT-driven WriteBody 失败。 */
            if (!streamingHandoff && httpCtx->writer) {
                httpCtx->writer->End();
            }
            if (tracker) tracker(-1);
        };

        if (dispatcher) {
            dispatcher(task);
        } else {
            /* No async dispatcher, degrade to sync execution */
            task();
        }
        return 0; /* HANDLE_CONTINUE - libhv does not commit response */
    };
}

void Router::bind(hv::HttpService& service) {
    /* Share one middleware chain across all route handlers (avoid N copies) */
    auto chain = std::make_shared<MiddlewareChain>(m_middlewares);

    for (size_t i = 0; i < m_routes.size(); ++i) {
        const Route& r = m_routes[i];
        if (r.async) {
            service.GET(r.path.c_str(), makeAsyncHandler(r.handler, chain, m_asyncDispatcher, m_asyncTaskTracker));
            continue;
        }
        auto h = makeSyncHandler(r.handler, chain);
        switch (r.method) {
            case Method::GET:    service.GET(r.path.c_str(), h);    break;
            case Method::POST:   service.POST(r.path.c_str(), h);   break;
            case Method::PUT:    service.PUT(r.path.c_str(), h);    break;
            case Method::DELETE: service.Delete(r.path.c_str(), h); break;
            case Method::PATCH:  service.PATCH(r.path.c_str(), h);  break;
        }
    }

    /* Postprocessor */
    if (m_postprocessor) {
        std::function<int(Context&)> pp = m_postprocessor;
        service.postprocessor = [pp](HttpRequest* req, HttpResponse* resp) -> int {
            Context ctx(static_cast<void*>(req), static_cast<void*>(resp));
            return pp(ctx);
        };
    }

    /* Error handler */
    if (m_errorHandler) {
        std::function<int(Context&)> eh = m_errorHandler;
        service.errorHandler = [eh](HttpRequest* req, HttpResponse* resp) -> int {
            Context ctx(static_cast<void*>(req), static_cast<void*>(resp));
            return eh(ctx);
        };
    }
}

} // namespace fw
} // namespace alkaidlab
