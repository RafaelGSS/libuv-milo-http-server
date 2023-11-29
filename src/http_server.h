#pragma once
#include <napi.h>

class MiloHttpServer : public Napi::ObjectWrap<MiloHttpServer>
{
public:
    MiloHttpServer(const Napi::CallbackInfo &);
    Napi::Value Listen(const Napi::CallbackInfo &);

    static Napi::Function GetClass(Napi::Env);
};