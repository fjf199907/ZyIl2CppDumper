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
#include <pthread.h>        // ★ 新增
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"
#include <sys/stat.h>

#define DO_API(r, n, p) r (*n) p

#include "il2cpp-api-functions.h"

#undef DO_API

static uint64_t il2cpp_base = 0;
static void *g_il2cpp_handle = nullptr;   // ★ 新增: 供 health 扫描线程使用

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

// Format a const field's value; only primitives and strings are representable, else "".
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
    g_il2cpp_handle = handle;          // ★ 新增: 保存 handle
    init_il2cpp_api(handle);
    xdl_info_t xinfo{};
    if (xdl_info(handle, XDL_DI_DLINFO, &xinfo) == 0 && xinfo.dli_fbase) {
        il2cpp_base = reinterpret_cast<uint64_t>(xinfo.dli_fbase);
    }
    LOGI("il2cpp_base: %" PRIx64"", il2cpp_base);
}

// Scoped SIGSEGV/SIGBUS guard: skip decoy classes whose malformed metadata
// faults inside libil2cpp, instead of crashing the whole dump.
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
// --- 追加 ---
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
// --- /追加 ---

// ===========================================================================
// ★ 新增: 运行时枚举 Health / CharacterStat / PlayerStates / HealthBar
// 因为这几个是 HybridCLR 热更类, 不在 global-metadata.dat 里, Il2CppDumper 看不到。
// 直接走 il2cpp domain/assembly/image/class/field 的运行时 API, 打印字段偏移。
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
    LOGI("  domain_get=%p dga=%p agi=%p", api.domain_get, api.domain_get_assemblies, api.assembly_get_image);
    LOGI("  igcc=%p igc=%p ign=%p", api.image_get_class_count, api.image_get_class, api.image_get_name);
    LOGI("  cgn=%p cgn2=%p cgf=%p", api.class_get_namespace, api.class_get_name, api.class_get_fields);
    LOGI("  fgn=%p fgo=%p fgt=%p cft=%p",
         api.field_get_name, api.field_get_offset, api.field_get_type, api.class_from_type);
    return ok;
}

static int scan_health_classes_once(FILE *out, HealthScanApi &api) {
    void *domain = api.domain_get();
    if (!domain) return -1;

    size_t asmCount = 0;
    void **assemblies = (void **)api.domain_get_assemblies(domain, &asmCount);
    if (!assemblies) return -1;
    if (out) fprintf(out, "assemblies count = %zu\n", asmCount);

    int hits = 0;
    for (size_t i = 0; i < asmCount; ++i) {
        void *image = api.assembly_get_image(assemblies[i]);
        if (!image) continue;

        const char *imageName = api.image_get_name ? api.image_get_name(image) : "?";
        size_t classCount = api.image_get_class_count(image);

        for (size_t j = 0; j < classCount; ++j) {
            void *klass = api.image_get_class(image, j);
            if (!klass) continue;

            const char *ns   = api.class_get_namespace(klass);
            const char *name = api.class_get_name(klass);
            if (!name) continue;

            bool hit = strstr(name, "Health")
                    || strstr(name, "CharacterStat")
                    || strstr(name, "PlayerStates")
                    || strstr(name, "HealthBar")
                    || (ns && strstr(ns, "Health"));
            if (!hit) continue;

            ++hits;
            if (out) {
                fprintf(out, "\n=== image=%s class %s.%s ===\n",
                        imageName ? imageName : "?", ns ? ns : "", name);
            }

            void *fiter = nullptr;
            while (auto field = api.class_get_fields(klass, &fiter)) {
                void *fieldType = api.field_get_type(field);
                const char *typeName = "?";
                if (fieldType) {
                    void *fieldClass = api.class_from_type(fieldType);
                    if (fieldClass) {
                        const char *tn = api.class_get_name(fieldClass);
                        if (tn) typeName = tn;
                    }
                }
                if (out) {
                    fprintf(out, "  field %-32s offset=0x%zx type=%s\n",
                            api.field_get_name(field),
                            api.field_get_offset(field),
                            typeName);
                }
            }
        }
    }
    if (out) fflush(out);
    return hits;
}

static void* health_scanner_thread(void *arg) {
    void *handle = arg;

    HealthScanApi api;
    if (!resolve_health_api(api, handle)) {
        LOGE("health scan: resolve api failed, abort");
        return nullptr;
    }

    const char *path = "/data/user/0/com.pinkcore.majo.erolabs/files/health_classes.txt";
    FILE *out = fopen(path, "w");
    if (!out) LOGE("health scan: open %s failed", path);
    LOGI("health scan: start, out=%s", path);

    for (int round = 0; round < 300; ++round) {
        sleep(2);
        int hits = scan_health_classes_once(out, api);
        if (hits > 0) {
            LOGI("health scan: round %d hits=%d, done", round, hits);
            if (out) { fclose(out); out = nullptr; }
            return nullptr;
        }
        if ((round % 5) == 0) {
            LOGI("health scan: round %d hits=0, keep waiting", round);
        }
    }

    if (out) fclose(out);
    LOGI("health scan: timeout, no class found");
    return nullptr;
}

// ===========================================================================
// ★ /新增
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

                // ★ 新增: 启动 health 扫描线程
                if (g_il2cpp_handle) {
                    pthread_t th;
                    if (pthread_create(&th, nullptr, health_scanner_thread, g_il2cpp_handle) == 0) {
                        pthread_detach(th);
                        LOGI("health scanner thread started");
                    } else {
                        LOGE("failed to create health scanner thread");
                    }
                } else {
                    LOGE("g_il2cpp_handle is null, skip health scanner");
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
