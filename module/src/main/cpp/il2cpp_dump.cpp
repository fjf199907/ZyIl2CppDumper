//
// Created by Perfare on 2020/7/4.
//

#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <link.h>
#include <csetjmp>
#include <csignal>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>        // ★ 新增
#include <netinet/in.h>        // ★ 新增
#include <arpa/inet.h>         // ★ 新增
#include <errno.h>             // ★ 新增
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"

#define DO_API(r, n, p) r (*n) p

#include "il2cpp-api-functions.h"

#undef DO_API

static uint64_t il2cpp_base = 0;
static void *g_il2cpp_handle = nullptr;

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) {                      \
    n = (r (*) p)xdl_sym(handle, #n, nullptr); \
    if(!n) {                                   \
        LOGW("api not found %s", #n);          \
    }                                          \
}

#include "il2cpp-api-functions.h"

#undef DO_API
}

std::string get_method_modifier(uint32_t flags) {
    std::stringstream outPut;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE:
            outPut << "private ";
            break;
        case METHOD_ATTRIBUTE_PUBLIC:
            outPut << "public ";
            break;
        case METHOD_ATTRIBUTE_FAMILY:
            outPut << "protected ";
            break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM:
            outPut << "internal ";
            break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) {
        outPut << "static ";
    }
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "sealed override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT) {
            outPut << "virtual ";
        } else {
            outPut << "override ";
        }
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
        outPut << "extern ";
    }
    return outPut.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    auto byref = type->byref;
    if (il2cpp_type_is_byref) {
        byref = il2cpp_type_is_byref(type);
    }
    return byref;
}

std::string get_field_default_value(FieldInfo *field, const Il2CppType *field_type) {
    std::stringstream outPut;
    if (!il2cpp_field_static_get_value) {
        return outPut.str();
    }
    uint64_t val = 0;
    il2cpp_field_static_get_value(field, &val);
    switch (field_type->type) {
        case IL2CPP_TYPE_BOOLEAN:
            outPut << ((val & 0xff) ? "true" : "false");
            break;
        case IL2CPP_TYPE_CHAR:
            outPut << (uint32_t) (uint16_t) val;
            break;
        case IL2CPP_TYPE_I1:
            outPut << (int32_t) (int8_t) val;
            break;
        case IL2CPP_TYPE_U1:
            outPut << (uint32_t) (uint8_t) val;
            break;
        case IL2CPP_TYPE_I2:
            outPut << (int32_t) (int16_t) val;
            break;
        case IL2CPP_TYPE_U2:
            outPut << (uint32_t) (uint16_t) val;
            break;
        case IL2CPP_TYPE_I4:
            outPut << (int32_t) val;
            break;
        case IL2CPP_TYPE_U4:
            outPut << (uint32_t) val;
            break;
        case IL2CPP_TYPE_I8:
            outPut << (int64_t) val;
            break;
        case IL2CPP_TYPE_U8:
            outPut << val;
            break;
        case IL2CPP_TYPE_R4: {
            float f = 0;
            memcpy(&f, &val, sizeof(f));
            outPut << f;
            break;
        }
        case IL2CPP_TYPE_R8: {
            double d = 0;
            memcpy(&d, &val, sizeof(d));
            outPut << d;
            break;
        }
        case IL2CPP_TYPE_STRING: {
            auto str = (Il2CppString *) val;
            if (!str) {
                outPut << "null";
            } else if (il2cpp_string_chars && il2cpp_string_length) {
                auto chars = il2cpp_string_chars(str);
                auto len = il2cpp_string_length(str);
                outPut << "\"";
                for (int i = 0; i < len; ++i) {
                    Il2CppChar c = chars[i];
                    switch (c) {
                        case '\\': outPut << "\\\\"; break;
                        case '\"': outPut << "\\\""; break;
                        case '\n': outPut << "\\n"; break;
                        case '\r': outPut << "\\r"; break;
                        case '\t': outPut << "\\t"; break;
                        default:
                            if (c >= 0x20 && c < 0x7f) {
                                outPut << (char) c;
                            } else {
                                char buf[8];
                                snprintf(buf, sizeof(buf), "\\u%04x", c);
                                outPut << buf;
                            }
                    }
                }
                outPut << "\"";
            }
            break;
        }
        default:
            break;
    }
    return outPut.str();
}

std::string dump_method(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        if (method->methodPointer) {
            outPut << "\t// RVA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer - il2cpp_base;
            outPut << " VA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer;
        } else {
            outPut << "\t// RVA: 0x VA: 0x0";
        }
        outPut << "\n\t";
        uint32_t iflags = 0;
        auto flags = il2cpp_method_get_flags(method, &iflags);
        outPut << get_method_modifier(flags);
        auto return_type = il2cpp_method_get_return_type(method);
        if (_il2cpp_type_is_byref(return_type)) {
            outPut << "ref ";
        }
        auto return_class = il2cpp_class_from_type(return_type);
        outPut << il2cpp_class_get_name(return_class) << " " << il2cpp_method_get_name(method)
               << "(";
        auto param_count = il2cpp_method_get_param_count(method);
        for (int i = 0; i < param_count; ++i) {
            auto param = il2cpp_method_get_param(method, i);
            auto attrs = param->attrs;
            if (_il2cpp_type_is_byref(param)) {
                if (attrs & PARAM_ATTRIBUTE_OUT && !(attrs & PARAM_ATTRIBUTE_IN)) {
                    outPut << "out ";
                } else if (attrs & PARAM_ATTRIBUTE_IN && !(attrs & PARAM_ATTRIBUTE_OUT)) {
                    outPut << "in ";
                } else {
                    outPut << "ref ";
                }
            } else {
                if (attrs & PARAM_ATTRIBUTE_IN) {
                    outPut << "[In] ";
                }
                if (attrs & PARAM_ATTRIBUTE_OUT) {
                    outPut << "[Out] ";
                }
            }
            auto parameter_class = il2cpp_class_from_type(param);
            outPut << il2cpp_class_get_name(parameter_class) << " "
                   << il2cpp_method_get_param_name(method, i);
            outPut << ", ";
        }
        if (param_count > 0) {
            outPut.seekp(-2, std::stringstream::cur);
        }
        outPut << ") { }\n";
    }
    return outPut.str();
}

std::string dump_property(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Properties\n";
    void *iter = nullptr;
    while (auto prop_const = il2cpp_class_get_properties(klass, &iter)) {
        auto prop = const_cast<PropertyInfo *>(prop_const);
        auto get = il2cpp_property_get_get_method(prop);
        auto set = il2cpp_property_get_set_method(prop);
        auto prop_name = il2cpp_property_get_name(prop);
        outPut << "\t";
        Il2CppClass *prop_class = nullptr;
        uint32_t iflags = 0;
        if (get) {
            outPut << get_method_modifier(il2cpp_method_get_flags(get, &iflags));
            prop_class = il2cpp_class_from_type(il2cpp_method_get_return_type(get));
        } else if (set) {
            outPut << get_method_modifier(il2cpp_method_get_flags(set, &iflags));
            auto param = il2cpp_method_get_param(set, 0);
            prop_class = il2cpp_class_from_type(param);
        }
        if (prop_class) {
            outPut << il2cpp_class_get_name(prop_class) << " " << prop_name << " { ";
            if (get) {
                outPut << "get; ";
            }
            if (set) {
                outPut << "set; ";
            }
            outPut << "}\n";
        } else {
            if (prop_name) {
                outPut << " // unknown property " << prop_name;
            }
        }
    }
    return outPut.str();
}

std::string dump_field(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";
    auto is_enum = il2cpp_class_is_enum(klass);
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        outPut << "\t";
        auto attrs = il2cpp_field_get_flags(field);
        auto access = attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK;
        switch (access) {
            case FIELD_ATTRIBUTE_PRIVATE:
                outPut << "private ";
                break;
            case FIELD_ATTRIBUTE_PUBLIC:
                outPut << "public ";
                break;
            case FIELD_ATTRIBUTE_FAMILY:
                outPut << "protected ";
                break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM:
                outPut << "internal ";
                break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:
                outPut << "protected internal ";
                break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            outPut << "const ";
        } else {
            if (attrs & FIELD_ATTRIBUTE_STATIC) {
                outPut << "static ";
            }
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) {
                outPut << "readonly ";
            }
        }
        auto field_type = il2cpp_field_get_type(field);
        auto field_class = il2cpp_class_from_type(field_type);
        outPut << il2cpp_class_get_name(field_class) << " " << il2cpp_field_get_name(field);
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            if (is_enum) {
                uint64_t val = 0;
                il2cpp_field_static_get_value(field, &val);
                outPut << " = " << std::dec << val;
            } else {
                auto default_value = get_field_default_value(field, field_type);
                if (!default_value.empty()) {
                    outPut << " = " << default_value;
                }
            }
        }
        outPut << "; // 0x" << std::hex << il2cpp_field_get_offset(field) << "\n";
    }
    return outPut.str();
}

std::string dump_type(const Il2CppType *type) {
    std::stringstream outPut;
    auto *klass = il2cpp_class_from_type(type);
    outPut << "\n// Namespace: " << il2cpp_class_get_namespace(klass) << "\n";
    auto flags = il2cpp_class_get_flags(klass);
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) {
        outPut << "[Serializable]\n";
    }
    auto is_valuetype = il2cpp_class_is_valuetype(klass);
    auto is_enum = il2cpp_class_is_enum(klass);
    auto visibility = flags & TYPE_ATTRIBUTE_VISIBILITY_MASK;
    switch (visibility) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:
            outPut << "public ";
            break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY:
            outPut << "internal ";
            break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:
            outPut << "private ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:
            outPut << "protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & TYPE_ATTRIBUTE_ABSTRACT && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "static ";
    } else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && flags & TYPE_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
    } else if (!is_valuetype && !is_enum && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "sealed ";
    }
    if (flags & TYPE_ATTRIBUTE_INTERFACE) {
        outPut << "interface ";
    } else if (is_enum) {
        outPut << "enum ";
    } else if (is_valuetype) {
        outPut << "struct ";
    } else {
        outPut << "class ";
    }
    outPut << il2cpp_class_get_name(klass);
    std::vector<std::string> extends;
    auto parent = il2cpp_class_get_parent(klass);
    if (!is_valuetype && !is_enum && parent) {
        auto parent_type = il2cpp_class_get_type(parent);
        if (parent_type->type != IL2CPP_TYPE_OBJECT) {
            extends.emplace_back(il2cpp_class_get_name(parent));
        }
    }
    void *iter = nullptr;
    while (auto itf = il2cpp_class_get_interfaces(klass, &iter)) {
        extends.emplace_back(il2cpp_class_get_name(itf));
    }
    if (!extends.empty()) {
        outPut << " : " << extends[0];
        for (int i = 1; i < extends.size(); ++i) {
            outPut << ", " << extends[i];
        }
    }
    outPut << "\n{";
    outPut << dump_field(klass);
    outPut << dump_property(klass);
    outPut << dump_method(klass);
    outPut << "}\n";
    return outPut.str();
}

void il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    g_il2cpp_handle = handle;
    init_il2cpp_api(handle);
    xdl_info_t xinfo{};
    if (xdl_info(handle, XDL_DI_DLINFO, &xinfo) == 0 && xinfo.dli_fbase) {
        il2cpp_base = reinterpret_cast<uint64_t>(xinfo.dli_fbase);
    }
    LOGI("il2cpp_base: %" PRIx64"", il2cpp_base);
}

static sigjmp_buf g_dump_jmp;
static volatile sig_atomic_t g_dump_guard_active = 0;
static pid_t g_dump_tid = 0;
static struct sigaction g_old_segv{};
static struct sigaction g_old_bus{};

static void dump_fault_handler(int sig, siginfo_t *info, void *ucontext) {
    if (g_dump_guard_active && gettid() == g_dump_tid) {
        siglongjmp(g_dump_jmp, sig);
    }
    struct sigaction *old = (sig == SIGBUS) ? &g_old_bus : &g_old_segv;
    if (old->sa_flags & SA_SIGINFO) {
        if (old->sa_sigaction) old->sa_sigaction(sig, info, ucontext);
    } else if (old->sa_handler == SIG_IGN || old->sa_handler == SIG_DFL) {
        signal(sig, SIG_DFL);
        raise(sig);
    } else if (old->sa_handler) {
        old->sa_handler(sig);
    }
}

static void install_dump_guard() {
    g_dump_tid = gettid();
    struct sigaction sa{};
    sa.sa_sigaction = dump_fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_old_segv);
    sigaction(SIGBUS,  &sa, &g_old_bus);
}

static void remove_dump_guard() {
    g_dump_guard_active = 0;
    sigaction(SIGSEGV, &g_old_segv, nullptr);
    sigaction(SIGBUS,  &g_old_bus,  nullptr);
}

// ---------------------------------------------------------------------------
void dump_il2cpp_so(const char *outDir) {
    struct Seg { uint64_t start, end; };
    std::vector<Seg> segs;
    uint64_t base = UINT64_MAX, hi = 0;

    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) { LOGE("cannot open maps"); return; }
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        uint64_t s = 0, e = 0;
        char perms[8] = {0}, path[512] = {0};
        if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s %*s %*s %*s %511s",
                   &s, &e, perms, path) != 4) continue;
        if (!strstr(path, "libil2cpp.so")) continue;
        if (perms[0] != 'r') continue;
        if (s < base) base = s;
        if (e > hi)   hi = e;
        segs.push_back({s, e});
    }
    fclose(fp);

    if (segs.empty() || base == UINT64_MAX) {
        LOGE("libil2cpp.so not found in maps");
        return;
    }

    uint64_t total = hi - base;
    LOGI("libil2cpp.so base=0x%" PRIx64 " hi=0x%" PRIx64 " size=%" PRIu64,
         base, hi, total);

    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) { LOGE("calloc %" PRIu64 " failed", total); return; }

    for (auto &sg : segs) {
        uint64_t off = sg.start - base;
        uint64_t len = sg.end - sg.start;
        if (off + len > total) continue;
        if (sigsetjmp(g_dump_jmp, 1) == 0) {
            memcpy(buf + off, (void *)sg.start, len);
        } else {
            LOGW("segment 0x%" PRIx64 "-0x%" PRIx64 " faulted, zero-filled",
                 sg.start, sg.end);
        }
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/files/libil2cpp_dump.so", outDir);
    FILE *out = fopen(path, "wb");
    if (out) {
        size_t w = fwrite(buf, 1, total, out);
        fclose(out);
        LOGI("dumped libil2cpp.so %zu bytes -> %s", w, path);
    }
    free(buf);
}

// ===========================================================================
// ★ RPC server
// ===========================================================================

struct HealthScanApi {
    void* (*domain_get)();
    void* (*domain_get_assemblies)(void*, size_t*);
    void* (*assembly_get_image)(void*);
    size_t (*image_get_class_count)(void*);
    void* (*image_get_class)(void*, size_t);
    const char* (*image_get_name)(void*);
    const char* (*class_get_namespace)(void*);
    const char* (*class_get_name)(void*);
    void* (*class_get_fields)(void*, void**);
    const char* (*field_get_name)(void*);
    size_t (*field_get_offset)(void*);
    void* (*field_get_type)(void*);
    void* (*class_from_type)(void*);
};

static bool resolve_health_api(HealthScanApi &api, void *handle) {
    memset(&api, 0, sizeof(api));
    api.domain_get            = (decltype(api.domain_get))           xdl_sym(handle, "il2cpp_domain_get", nullptr);
    api.domain_get_assemblies = (decltype(api.domain_get_assemblies))xdl_sym(handle, "il2cpp_domain_get_assemblies", nullptr);
    api.assembly_get_image    = (decltype(api.assembly_get_image))   xdl_sym(handle, "il2cpp_assembly_get_image", nullptr);
    api.image_get_class_count = (decltype(api.image_get_class_count))xdl_sym(handle, "il2cpp_image_get_class_count", nullptr);
    api.image_get_class       = (decltype(api.image_get_class))      xdl_sym(handle, "il2cpp_image_get_class", nullptr);
    api.image_get_name        = (decltype(api.image_get_name))       xdl_sym(handle, "il2cpp_image_get_name", nullptr);
    api.class_get_namespace   = (decltype(api.class_get_namespace))  xdl_sym(handle, "il2cpp_class_get_namespace", nullptr);
    api.class_get_name        = (decltype(api.class_get_name))       xdl_sym(handle, "il2cpp_class_get_name", nullptr);
    api.class_get_fields      = (decltype(api.class_get_fields))     xdl_sym(handle, "il2cpp_class_get_fields", nullptr);
    api.field_get_name        = (decltype(api.field_get_name))       xdl_sym(handle, "il2cpp_field_get_name", nullptr);
    api.field_get_offset      = (decltype(api.field_get_offset))     xdl_sym(handle, "il2cpp_field_get_offset", nullptr);
    api.field_get_type        = (decltype(api.field_get_type))       xdl_sym(handle, "il2cpp_field_get_type", nullptr);
    api.class_from_type       = (decltype(api.class_from_type))      xdl_sym(handle, "il2cpp_class_from_type", nullptr);

    bool ok = api.domain_get && api.domain_get_assemblies && api.assembly_get_image
              && api.image_get_class_count && api.image_get_class
              && api.class_get_namespace && api.class_get_name && api.class_get_fields
              && api.field_get_name && api.field_get_offset && api.field_get_type
              && api.class_from_type;
    LOGI("resolve_health_api: %s", ok ? "ok" : "FAILED");
    return ok;
}

static HealthScanApi g_rpc_api;
static bool g_rpc_api_ok = false;

static void rpc_dump_class(FILE *out, void *klass) {
    const char *ns   = g_rpc_api.class_get_namespace(klass);
    const char *name = g_rpc_api.class_get_name(klass);
    fprintf(out, "=== class %s.%s ===\n", ns ? ns : "", name ? name : "?");

    void *fiter = nullptr;
    while (auto field = g_rpc_api.class_get_fields(klass, &fiter)) {
        void *fieldType = g_rpc_api.field_get_type(field);
        const char *typeName = "?";
        if (fieldType) {
            void *fieldClass = g_rpc_api.class_from_type(fieldType);
            if (fieldClass) {
                const char *tn = g_rpc_api.class_get_name(fieldClass);
                if (tn) typeName = tn;
            }
        }
        fprintf(out, "  field %-40s offset=0x%zx type=%s\n",
                g_rpc_api.field_get_name(field),
                g_rpc_api.field_get_offset(field),
                typeName);
    }
}

static int rpc_scan(FILE *out, const char *imagePattern,
                    const char *classPattern, bool dumpAllInMatchedImage) {
    void *domain = g_rpc_api.domain_get();
    if (!domain) { fprintf(out, "ERR: domain_get failed\n"); return -1; }

    size_t asmCount = 0;
    void **assemblies = (void **)g_rpc_api.domain_get_assemblies(domain, &asmCount);
    if (!assemblies) { fprintf(out, "ERR: dga failed\n"); return -1; }
    fprintf(out, "assemblies count = %zu\n", asmCount);

    int hits = 0;
    for (size_t i = 0; i < asmCount; ++i) {
        void *image = g_rpc_api.assembly_get_image(assemblies[i]);
        if (!image) continue;

        const char *imageName = g_rpc_api.image_get_name
                              ? g_rpc_api.image_get_name(image) : "?";
        if (!imageName) imageName = "?";

        bool imageMatched = imagePattern && strstr(imageName, imagePattern);
        bool dumpAll = dumpAllInMatchedImage && imageMatched;
        if (imagePattern && !imageMatched && !classPattern) continue;

        size_t classCount = g_rpc_api.image_get_class_count(image);
        for (size_t j = 0; j < classCount; ++j) {
            void *klass = g_rpc_api.image_get_class(image, j);
            if (!klass) continue;
            const char *name = g_rpc_api.class_get_name(klass);
            if (!name) continue;

            bool hit = dumpAll;
            if (!hit && classPattern) hit = strstr(name, classPattern) != nullptr;
            if (!hit && imagePattern) hit = imageMatched;
            if (!hit) continue;

            ++hits;
            fprintf(out, "\n[image=%s]\n", imageName);
            rpc_dump_class(out, klass);
        }
    }
    fprintf(out, "\n--- total %d ---\n", hits);
    return hits;
}

static void rpc_handle(const char *cmd, FILE *out) {
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (!*cmd) return;

    if (strcmp(cmd, "ping") == 0) {
        fprintf(out, "pong\n");
    }
    else if (strcmp(cmd, "base") == 0) {
        fprintf(out, "il2cpp_base=0x%" PRIx64 "\n", il2cpp_base);
    }
    else if (strcmp(cmd, "images") == 0) {
        void *domain = g_rpc_api.domain_get();
        size_t asmCount = 0;
        void **assemblies = (void **)g_rpc_api.domain_get_assemblies(domain, &asmCount);
        for (size_t i = 0; i < asmCount; ++i) {
            void *image = g_rpc_api.assembly_get_image(assemblies[i]);
            if (!image) continue;
            const char *n = g_rpc_api.image_get_name ? g_rpc_api.image_get_name(image) : "?";
            fprintf(out, "%s\n", n ? n : "?");
        }
    }
    else if (strncmp(cmd, "scan ", 5) == 0) {
        char img[128] = {0}, cls[128] = {0};
        int n = sscanf(cmd + 5, "%127s %127s", img, cls);
        const char *imgP = (n >= 1 && strcmp(img, "_") != 0) ? img : nullptr;
        const char *clsP = (n >= 2 && strcmp(cls, "_") != 0) ? cls : nullptr;
        rpc_scan(out, imgP, clsP, false);
    }
    else if (strncmp(cmd, "dumpimage ", 10) == 0) {
        char img[128] = {0};
        if (sscanf(cmd + 10, "%127s", img) == 1) {
            rpc_scan(out, img, nullptr, true);
        }
    }
    else if (strncmp(cmd, "class ", 6) == 0) {
        char cls[256] = {0};
        if (sscanf(cmd + 6, "%255s", cls) == 1) {
            void *domain = g_rpc_api.domain_get();
            size_t asmCount = 0;
            void **assemblies = (void **)g_rpc_api.domain_get_assemblies(domain, &asmCount);
            int found = 0;
            for (size_t i = 0; i < asmCount; ++i) {
                void *image = g_rpc_api.assembly_get_image(assemblies[i]);
                if (!image) continue;
                size_t classCount = g_rpc_api.image_get_class_count(image);
                for (size_t j = 0; j < classCount; ++j) {
                    void *klass = g_rpc_api.image_get_class(image, j);
                    if (!klass) continue;
                    const char *name = g_rpc_api.class_get_name(klass);
                    const char *ns   = g_rpc_api.class_get_namespace(klass);
                    if (!name) continue;
                    char full[512];
                    snprintf(full, sizeof(full), "%s.%s", ns ? ns : "", name);
                    if (strcmp(full, cls) == 0 || strcmp(name, cls) == 0) {
                        rpc_dump_class(out, klass);
                        found++;
                    }
                }
            }
            fprintf(out, "\n--- found %d ---\n", found);
        }
    }
    else if (strncmp(cmd, "read ", 5) == 0) {
        uint64_t addr = 0; int size = 0;
        if (sscanf(cmd + 5, "%" SCNx64 " %d", &addr, &size) == 2 && size > 0 && size <= 4096) {
            uint8_t *p = (uint8_t *)addr;
            for (int i = 0; i < size; i += 16) {
                fprintf(out, "%016" PRIx64 "  ", addr + i);
                for (int k = 0; k < 16 && i + k < size; ++k)
                    fprintf(out, "%02x ", p[i + k]);
                fprintf(out, " |");
                for (int k = 0; k < 16 && i + k < size; ++k) {
                    char c = p[i + k];
                    fprintf(out, "%c", (c >= 0x20 && c < 0x7f) ? c : '.');
                }
                fprintf(out, "|\n");
            }
        } else {
            fprintf(out, "ERR: usage: read <addr_hex> <size>\n");
        }
    }
    else if (strncmp(cmd, "write ", 6) == 0) {
        uint64_t addr = 0; char hex[1024] = {0};
        if (sscanf(cmd + 6, "%" SCNx64 " %1023s", &addr, hex) == 2) {
            size_t hlen = strlen(hex);
            if (hlen % 2 != 0) { fprintf(out, "ERR: hex len odd\n"); }
            else {
                uint8_t *p = (uint8_t *)addr;
                for (size_t i = 0; i < hlen; i += 2) {
                    unsigned v; sscanf(hex + i, "%2x", &v);
                    p[i / 2] = (uint8_t)v;
                }
                fprintf(out, "ok, wrote %zu bytes\n", hlen / 2);
            }
        } else {
            fprintf(out, "ERR: usage: write <addr_hex> <hex>\n");
        }
    }
    else if (strncmp(cmd, "readf ", 6) == 0) {
        uint64_t addr = 0;
        if (sscanf(cmd + 6, "%" SCNx64, &addr) == 1) {
            float f; memcpy(&f, (void*)addr, 4);
            fprintf(out, "float @0x%" PRIx64 " = %f\n", addr, f);
        }
    }
    else if (strncmp(cmd, "readi ", 6) == 0) {
        uint64_t addr = 0;
        if (sscanf(cmd + 6, "%" SCNx64, &addr) == 1) {
            int32_t v; memcpy(&v, (void*)addr, 4);
            fprintf(out, "int32 @0x%" PRIx64 " = %d\n", addr, v);
        }
    }
    else {
        fprintf(out, "ERR: unknown cmd\n");
        fprintf(out, "cmds: ping | base | images | scan <img> <cls> | "
                     "dumpimage <img> | class <name> | "
                     "read <addr> <sz> | write <addr> <hex> | "
                     "readf <addr> | readi <addr>\n");
    }
}

static void* rpc_server_thread(void*) {
    if (!resolve_health_api(g_rpc_api, g_il2cpp_handle)) {
        LOGE("rpc: resolve il2cpp api failed");
        return nullptr;
    }
    g_rpc_api_ok = true;
    LOGI("rpc: api resolved, starting server");

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { LOGE("rpc: socket failed %s", strerror(errno)); return nullptr; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(27042);
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("rpc: bind 27042 failed: %s", strerror(errno));
        close(srv);
        return nullptr;
    }
    listen(srv, 4);
    LOGI("rpc: listening on 127.0.0.1:27042");

    while (true) {
        int cli = accept(srv, nullptr, nullptr);
        if (cli < 0) { usleep(100000); continue; }
        LOGI("rpc: client connected");

        char line[512];
        while (true) {
            int n = 0;
            while (n < (int)sizeof(line) - 1) {
                char c;
                ssize_t r = read(cli, &c, 1);
                if (r <= 0) goto cli_done;
                if (c == '\n') break;
                line[n++] = c;
            }
            line[n] = 0;
            if (n == 0) continue;

            char *buf = nullptr;
            size_t buflen = 0;
            FILE *out = open_memstream(&buf, &buflen);
            if (!out) { goto cli_done; }

            rpc_handle(line, out);
            fclose(out);

            if (buf) {
                write(cli, buf, buflen);
                free(buf);
            }
            char eof = 0x04;
            write(cli, &eof, 1);
        }
    cli_done:
        close(cli);
        LOGI("rpc: client disconnected");
    }
    return nullptr;
}

// ===========================================================================

void il2cpp_dump(const char *outDir) {
    LOGI("memory scan dump start");

    install_dump_guard();
    g_dump_guard_active = 1;

    char dirPath[512];
    snprintf(dirPath, sizeof(dirPath), "%s/files", outDir);
    mkdir(dirPath, 0777);

    char path[512];
    snprintf(path, sizeof(path), "%s/files/global-metadata.dat", outDir);

    const uint8_t MAGIC[4] = {0xAF, 0x1B, 0xB1, 0xFA};

    for (int attempt = 1; attempt <= 60; ++attempt) {
        sleep(1);

        struct Region { uint64_t start; uint64_t end; };
        std::vector<Region> regions;
        FILE *fp = fopen("/proc/self/maps", "r");
        if (!fp) { LOGE("cannot open maps"); break; }
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            uint64_t s = 0, e = 0;
            char perms[8] = {0};
            if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s", &s, &e, perms) != 3) continue;
            if (perms[0] != 'r') continue;
            if (e <= s) continue;
            uint64_t sz = e - s;
            if (sz < 0x10000 || sz > 512ull * 1024 * 1024) continue;
            regions.push_back({s, e});
        }
        fclose(fp);

        for (auto &r : regions) {
            if (sigsetjmp(g_dump_jmp, 1) != 0) {
                LOGW("region %" PRIx64 "-%" PRIx64 " faulted, skip", r.start, r.end);
                continue;
            }

            uint8_t *base = (uint8_t *)r.start;
            uint64_t sz = r.end - r.start;

            for (uint64_t off = 0; off + 16 < sz; off += 4) {
                if (base[off] != 0xAF) continue;
                if (memcmp(base + off, MAGIC, 4) != 0) continue;

                uint32_t version = 0;
                memcpy(&version, base + off + 4, 4);
                if (version < 20 || version > 31) continue;

                int32_t sl = 0;
                memcpy(&sl, base + off + 8, 4);
                if (sl < 0 || sl > 0x200000) continue;

                LOGI("HIT @ %" PRIx64 " ver=%u sl=0x%x (try %d)",
                     r.start + off, version, sl, attempt);

                uint64_t remaining = sz - off;
                uint64_t dumpSize = remaining > (128ull * 1024 * 1024)
                                    ? (128ull * 1024 * 1024) : remaining;

                if (sigsetjmp(g_dump_jmp, 1) != 0) {
                    LOGW("faulted while writing, skip");
                    continue;
                }
                FILE *out = fopen(path, "wb");
                if (!out) { LOGE("fopen fail"); continue; }
                size_t w = fwrite(base + off, 1, dumpSize, out);
                fclose(out);
                LOGI("dumped %zu bytes -> %s", w, path);

                dump_il2cpp_so(outDir);

                g_dump_guard_active = 0;
                remove_dump_guard();

                // ★ 启动 RPC server（取代 health_scanner_thread）
                if (g_il2cpp_handle) {
                    pthread_t th;
                    if (pthread_create(&th, nullptr, rpc_server_thread, nullptr) == 0) {
                        pthread_detach(th);
                        LOGI("rpc server thread started");
                    } else {
                        LOGE("failed to create rpc server thread");
                    }
                } else {
                    LOGE("g_il2cpp_handle is null, skip rpc server");
                }

                return;
            }
        }
        LOGI("attempt %d: %zu regions, no hit", attempt, regions.size());
    }

    g_dump_guard_active = 0;
    remove_dump_guard();
    LOGI("no metadata found");
}
