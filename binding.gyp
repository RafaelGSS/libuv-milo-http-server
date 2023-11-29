{
    "targets": [{
        "target_name": "http-server-addon",
        "cflags!": [ "-fno-exceptions" ],
        "cflags_cc!": [ "-fno-exceptions" ],
        "sources": [
            "src/main.cpp",
        ],
        'include_dirs': [
            "<!@(node -p \"require('node-addon-api').include\")",
            "deps/milo/parser/dist/cpp/release-all-callbacks/"
        ],
        'dependencies': [
            "<!(node -p \"require('node-addon-api').gyp\")",
            "deps/uv/uv.gyp:libuv"
        ],
        'libraries': ['-Ldeps/milo/parser/dist/cpp/release-all-callbacks/libmilo.a'],
        'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
        'cflags': [ '-stdlib=libc++' ],
    }]
}