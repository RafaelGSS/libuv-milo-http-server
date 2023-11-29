#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <napi.h>
#include "uv.h"
#include "assert.h"
#include "milo.h"
#include "http_server.h"

// The backlog argument defines the maximum length to which the
// queue of pending connections for sockfd may grow.  If a
// connection request arrives when the queue is full, the client may
// receive an error with an indication of ECONNREFUSED
#define BACKLOG 511
#define HOST "0.0.0.0"

static uv_loop_t *loop;
static uv_tcp_t tcpServer;
static milo::Parser *parser;
struct sockaddr_in address;

struct milo_response
{
    const char *base;
    char *method;
    std::vector<std::string> header_values;
    std::vector<std::string> header_names;
    char *body;
};

struct ConnectionData
{
    Napi::FunctionReference cb;
};

static intptr_t on_method(const milo::Parser *p, uintptr_t position, uintptr_t size)
{
    milo_response *res = (milo_response *)p->owner;
    res->method = reinterpret_cast<char *>(malloc(sizeof(char) * 1000));
    strncpy(res->method, reinterpret_cast<const char *>(res->base) + position, size);
    return 0;
};

static intptr_t on_header_value(const milo::Parser *p, uintptr_t position, uintptr_t size)
{

    milo_response *res = (milo_response *)p->owner;
    char *content = reinterpret_cast<char *>(malloc(sizeof(char) * 1000));
    strncpy(content, reinterpret_cast<const char *>(res->base) + position, size);
    res->header_values.push_back(content);
    free(content);
    return 0;
};

static intptr_t on_header_name(const milo::Parser *p, uintptr_t position, uintptr_t size)
{
    milo_response *res = (milo_response *)p->owner;
    char *content = reinterpret_cast<char *>(malloc(sizeof(char) * 1000));
    strncpy(content, reinterpret_cast<const char *>(res->base) + position, size);
    res->header_names.push_back(content);
    free(content);
    return 0;
};

static intptr_t on_data(const milo::Parser *p, uintptr_t position, uintptr_t size)
{
    milo_response *res = (milo_response *)p->owner;
    res->body = reinterpret_cast<char *>(malloc(sizeof(char) * 1000));
    strncpy(res->body, reinterpret_cast<const char *>(res->base) + position, size);
    return 0;
};

static void
after_read(uv_stream_t *handle,
           ssize_t nread,
           const uv_buf_t *buf)
{

    if (nread <= 0)
    {
        free(buf->base);
        return;
    }

    milo_response *res = (milo_response *)malloc(sizeof(milo_response));
    res->base = buf->base;

    parser->owner = res;
    parser->callbacks.on_method = on_method;
    parser->callbacks.on_header_value = on_header_value;
    parser->callbacks.on_header_name = on_header_name;
    parser->callbacks.on_data = on_data;

    milo::milo_parse(parser, (const unsigned char *)buf->base, nread);

    ConnectionData *connectionData = static_cast<ConnectionData *>(tcpServer.data);

    Napi::Object responseObj = Napi::Object::New(connectionData->cb.Env());
    responseObj.Set("method", Napi::String::New(connectionData->cb.Env(), res->method));
    responseObj.Set("body", Napi::String::New(connectionData->cb.Env(), res->body));

    Napi::Array headerNamesArray = Napi::Array::New(connectionData->cb.Env(), res->header_names.size());
    for (size_t i = 0; i < res->header_names.size(); i++)
    {
        headerNamesArray.Set(i, Napi::String::New(connectionData->cb.Env(), res->header_names[i]));
    }

    responseObj.Set("header_names", headerNamesArray);

    Napi::Array headerValuesArray = Napi::Array::New(connectionData->cb.Env(), res->header_values.size());
    for (size_t i = 0; i < res->header_values.size(); i++)
    {
        headerValuesArray.Set(i, Napi::String::New(connectionData->cb.Env(), res->header_values[i]));
    }

    responseObj.Set("header_values", headerValuesArray);

    connectionData->cb.Call(connectionData->cb.Env().Global(), {responseObj});

    free(res);
}

// we have to allocate manually all chunks of data we receive
static void allocator(uv_handle_t *handle,
                      size_t suggested_size,
                      uv_buf_t *buf)
{
    buf->base = (char *)malloc(suggested_size);
    buf->len = suggested_size;
}

static void on_connection(uv_stream_t *server, int status)
{

    if (status < 0)
    {
        fprintf(stderr, "listen error: %s\n", uv_strerror(status));
        return;
    }

    uv_tcp_t *client = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));

    // initialize the client handle
    uv_tcp_init(loop, client);

    assert(uv_accept(server, (uv_stream_t *)client) == 0);

    uv_read_start((uv_stream_t *)client, allocator, after_read);
}

MiloHttpServer::MiloHttpServer(const Napi::CallbackInfo &info)
    : ObjectWrap(info)
{
}

Napi::Value MiloHttpServer::Listen(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 2)
    {
        Napi::TypeError::New(env, "Wrong number of arguments")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    uint port = info[0].As<Napi::Number>().DoubleValue();

    loop = uv_default_loop();
    parser = milo::milo_create();
    uv_tcp_init(loop, &tcpServer);
    printf("Starting on %s:%d\n", HOST, port);
    uv_ip4_addr(HOST, port, &address);
    uv_tcp_bind(&tcpServer, (const struct sockaddr *)&address, 0);

    ConnectionData *connectionData = new ConnectionData{Napi::Persistent(info[1].As<Napi::Function>())};
    tcpServer.data = connectionData;

    uv_listen((uv_stream_t *)&tcpServer, BACKLOG, on_connection);
    uv_run(loop, UV_RUN_DEFAULT);
}

Napi::Function MiloHttpServer::GetClass(Napi::Env env)
{
    return DefineClass(
        env,
        "MiloHttpServer",
        {
            MiloHttpServer::InstanceMethod("listen", &MiloHttpServer::Listen),
        });
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    Napi::String name = Napi::String::New(env, "MiloHttpServer");
    exports.Set(name, MiloHttpServer::GetClass(env));
    return exports;
}

NODE_API_MODULE(addon, Init)
