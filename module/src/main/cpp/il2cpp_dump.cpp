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
#include <algorithm>
#include <unistd.h>
#include <link.h>
#include <csetjmp>
#include <csignal>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
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
        case METHOD_ATTRIBUTE_PRIVATE:        outPut << "private "; break;
        case METHOD_ATTRIBUTE_PUBLIC:         outPut << "public "; break;
        case METHOD_ATTRIBUTE_FAMILY:         outPut << "protected "; break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM:  outPut << "internal "; break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:   outPut << "protected internal "; break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) outPut << "static ";
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT)
            outPut << "override ";
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT)
            outPut << "sealed override ";
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT)
            outPut << "virtual ";
        else
            outPut << "override ";
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) outPut << "extern ";
    return outPut.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    auto byref = type->byref;
    if (il2cpp_type_is_byref) byref = il2cpp_type_is_byref(type);
    return byref;
}

std::string get_field_default_value(FieldInfo *field, const Il2CppType *field_type) {
    std::stringstream outPut;
    if (!il2cpp_field_static_get_value) return outPut.str();
    uint64_t val = 0;
    il2cpp_field_static_get_value(field, &val);
    switch (field_type->type) {
        case IL2CPP_TYPE_BOOLEAN: outPut << ((val & 0xff) ? "true" : "false"); break;
        case IL2CPP_TYPE_CHAR:    outPut << (uint32_t) (uint16_t) val; break;
        case IL2CPP_TYPE_I1:      outPut << (int32_t) (int8_t) val; break;
        case IL2CPP_TYPE_U1:      outPut << (uint32_t) (uint8_t) val; break;
        case IL2CPP_TYPE_I2:      outPut << (int32_t) (int16_t) val; break;
        case IL2CPP_TYPE_U2:      outPut << (uint32_t) (uint16_t) val; break;
        case IL2CPP_TYPE_I4:      outPut << (int32_t) val; break;
        case IL2CPP_TYPE_U4:      outPut << (uint32_t) val; break;
        case IL2CPP_TYPE_I8:      outPut << (int64_t) val; break;
        case IL2CPP_TYPE_U8:      outPut << val; break;
        case IL2CPP_TYPE_R4: { float f; memcpy(&f, &val, 4); outPut << f; break; }
        case IL2CPP_TYPE_R8: { double d; memcpy(&d, &val, 8); outPut << d; break; }
        case IL2CPP_TYPE_STRING: {
            auto str = (Il2CppString *) val;
            if (!str) outPut << "null";
            else if (il2cpp_string_chars && il2cpp_string_length) {
                auto chars = il2cpp_string_chars(str);
                auto len = il2cpp_string_length(str);
                outPut << "\"";
                for (int i = 0; i < len; ++i) {
                    Il2CppChar c = chars[i];
                    if (c >= 0x20 && c < 0x7f) outPut << (char) c;
                    else { char buf[8]; snprintf(buf, 8, "\\u%04x", c); outPut << buf; }
                }
                outPut << "\"";
            }
            break;
        }
        default: break;
    }
    return outPut.str();
}

std::string dump_method(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        if (method->methodPointer) {
            outPut << "\t// RVA: 0x" << std::hex << (uint64_t) method->methodPointer - il2cpp_base
                   << " VA: 0x" << std::hex << (uint64_t) method->methodPointer;
        } else {
            outPut << "\t// RVA: 0x VA: 0x0";
        }
        outPut << "\n\t";
        uint32_t iflags = 0;
        auto flags = il2cpp_method_get_flags(method, &iflags);
        outPut << get_method_modifier(flags);
        auto return_type = il2cpp_method_get_return_type(method);
        if (_il2cpp_type_is_byref(return_type)) outPut << "ref ";
        auto return_class = il2cpp_class_from_type(return_type);
        outPut << il2cpp_class_get_name(return_class) << " " << il2cpp_method_get_name(method) << "(";
        auto param_count = il2cpp_method_get_param_count(method);
        for (int i = 0; i < param_count; ++i) {
            auto param = il2cpp_method_get_param(method, i);
            auto attrs = param->attrs;
            if (_il2cpp_type_is_byref(param)) {
                if (attrs & PARAM_ATTRIBUTE_OUT && !(attrs & PARAM_ATTRIBUTE_IN)) outPut << "out ";
                else if (attrs & PARAM_ATTRIBUTE_IN && !(attrs & PARAM_ATTRIBUTE_OUT)) outPut << "in ";
                else outPut << "ref ";
            }
            auto parameter_class = il2cpp_class_from_type(param);
            outPut << il2cpp_class_get_name(parameter_class) << " " << il2cpp_method_get_param_name(method, i) << ", ";
        }
        if (param_count > 0) outPut.seekp(-2, std::stringstream::cur);
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
            prop_class = il2cpp_class_from_type(il2cpp_method_get_param(set, 0));
        }
        if (prop_class) {
            outPut << il2cpp_class_get_name(prop_class) << " " << prop_name << " { ";
            if (get) outPut << "get; ";
            if (set) outPut << "set; ";
            outPut << "}\n";
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
            case FIELD_ATTRIBUTE_PRIVATE: outPut << "private "; break;
            case FIELD_ATTRIBUTE_PUBLIC:  outPut << "public "; break;
            case FIELD_ATTRIBUTE_FAMILY:  outPut << "protected "; break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM: outPut << "internal "; break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:  outPut << "protected internal "; break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) outPut << "const ";
        else {
            if (attrs & FIELD_ATTRIBUTE_STATIC) outPut << "static ";
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) outPut << "readonly ";
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
                if (!default_value.empty()) outPut << " = " << default_value;
            }
        }
        outPut << "; // 0x" << std::hex << il2cpp_field_get_offset(field) << "\n";
    }
    return outPut.str();
}

void il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    g_il2cpp_handle = handle;
    init_il2cpp_api(handle);
    xdl_info_t xinfo{};
    if (xdl_info(handle, XDL_DI_DLINFO, &xinfo) == 0 && xinfo.dli_fbase)
        il2cpp_base = reinterpret_cast<uint64_t>(xinfo.dli_fbase);
    LOGI("il2cpp_base: %" PRIx64"", il2cpp_base);
}

static sigjmp_buf g_dump_jmp;
static volatile sig_atomic_t g_dump_guard_active = 0;
static pid_t g_dump_tid = 0;
static struct sigaction g_old_segv{};
static struct sigaction g_old_bus{};

static void dump_fault_handler(int sig, siginfo_t *info, void *ucontext) {
    if (g_dump_guard_active && gettid() == g_dump_tid) siglongjmp(g_dump_jmp, sig);
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
        if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s %*s %*s %*s %511s", &s, &e, perms, path) != 4) continue;
        if (!strstr(path, "libil2cpp.so")) continue;
        if (perms[0] != 'r') continue;
        if (s < base) base = s;
        if (e > hi)   hi = e;
        segs.push_back({s, e});
    }
    fclose(fp);
    if (segs.empty() || base == UINT64_MAX) return;
    uint64_t total = hi - base;
    LOGI("libil2cpp.so base=0x%" PRIx64 " hi=0x%" PRIx64 " size=%" PRIu64, base, hi, total);
    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) return;
    for (auto &sg : segs) {
        uint64_t off = sg.start - base;
        uint64_t len = sg.end - sg.start;
        if (off + len > total) continue;
        if (sigsetjmp(g_dump_jmp, 1) == 0) memcpy(buf + off, (void *)sg.start, len);
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
// inline hook (ARM64) + spec 驱动
// ===========================================================================

enum SpecType {
    T_I8 = 0, T_U8, T_I16, T_U16, T_I32, T_U32, T_I64, T_U64, T_F32, T_F64, T_PTR
};

struct SpecItem {
    uint8_t src;    // 0=this, 1=a0, 2=a1, 3=a2, 4=a3
    int32_t off;
    uint8_t type;
};

struct HookRecord {
    uint64_t vals[8];
};

struct HookSlot {
    uint64_t target;
    uint8_t  saved[16];
    char     name[64];
    char     spec[128];
    volatile int  hit_count;
    volatile bool active;
    volatile uint32_t write_idx;
    pthread_mutex_t lock;

    int      field_count;
    SpecItem fields[8];
    HookRecord records[256];
};

static HookSlot g_hooks[4] = {};

static void hook_generic(int idx, void* a0, void* a1, void* a2, void* a3);

#define DEFINE_ENTRY(N) \
extern "C" void hook_entry_##N(void* a0, void* a1, void* a2, void* a3) { \
    hook_generic(N, a0, a1, a2, a3); \
}
DEFINE_ENTRY(0)
DEFINE_ENTRY(1)
DEFINE_ENTRY(2)
DEFINE_ENTRY(3)
#undef DEFINE_ENTRY

static void* g_entry_table[4] = {
    (void*)hook_entry_0, (void*)hook_entry_1,
    (void*)hook_entry_2, (void*)hook_entry_3,
};

// 16 字节跳转: LDR X17, #8 ; BR X17 ; <8-byte target>
static void write_jump(uint64_t target, uint64_t entry) {
    uint8_t code[16];
    uint32_t ldr = 0x58000051;
    uint32_t br  = 0xD61F0220;
    memcpy(code, &ldr, 4);
    memcpy(code + 4, &br, 4);
    memcpy(code + 8, &entry, 8);
    uint64_t page = target & ~0xFFFULL;
    mprotect((void*)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy((void*)target, code, 16);
    __builtin___clear_cache((char*)target, (char*)(target + 16));
}

static bool parse_spec(HookSlot& h, const char* spec) {
    h.field_count = 0;
    char buf[128];
    strncpy(buf, spec, 127); buf[127] = 0;
    char* saveptr = nullptr;
    char* tok = strtok_r(buf, ",", &saveptr);
    while (tok && h.field_count < 8) {
        while (*tok == ' ') tok++;
        SpecItem& it = h.fields[h.field_count];
        it.type = T_I64;
        it.off = 0;
        it.src = 0;

        char expr[64];
        char* colon = strrchr(tok, ':');
        if (colon) {
            *colon = 0;
            const char* t = colon + 1;
            if      (!strcmp(t,"i8"))  it.type=T_I8;
            else if (!strcmp(t,"u8"))  it.type=T_U8;
            else if (!strcmp(t,"i16")) it.type=T_I16;
            else if (!strcmp(t,"u16")) it.type=T_U16;
            else if (!strcmp(t,"i32")) it.type=T_I32;
            else if (!strcmp(t,"u32")) it.type=T_U32;
            else if (!strcmp(t,"i64")) it.type=T_I64;
            else if (!strcmp(t,"u64")) it.type=T_U64;
            else if (!strcmp(t,"f32")) it.type=T_F32;
            else if (!strcmp(t,"f64")) it.type=T_F64;
            else if (!strcmp(t,"ptr")) it.type=T_PTR;
        }
        strncpy(expr, tok, 63); expr[63]=0;

        char* plus = strchr(expr, '+');
        if (plus) { *plus = 0; it.off = (int32_t)strtol(plus+1, nullptr, 0); }

        if      (!strcmp(expr,"this")) it.src = 0;
        else if (!strcmp(expr,"a0"))   it.src = 1;
        else if (!strcmp(expr,"a1"))   it.src = 2;
        else if (!strcmp(expr,"a2"))   it.src = 3;
        else if (!strcmp(expr,"a3"))   it.src = 4;
        else return false;

        h.field_count++;
        tok = strtok_r(nullptr, ",", &saveptr);
    }
    return h.field_count > 0;
}

static void read_field(uint64_t addr, const SpecItem& it, uint64_t* out) {
    switch (it.type) {
        case T_I8:  { int8_t  v; memcpy(&v,(void*)addr,1); *out=(uint64_t)(int64_t)v; break; }
        case T_U8:  { uint8_t v; memcpy(&v,(void*)addr,1); *out=v; break; }
        case T_I16: { int16_t v; memcpy(&v,(void*)addr,2); *out=(uint64_t)(int64_t)v; break; }
        case T_U16: { uint16_t v;memcpy(&v,(void*)addr,2); *out=v; break; }
        case T_I32: { int32_t v; memcpy(&v,(void*)addr,4); *out=(uint64_t)(int64_t)v; break; }
        case T_U32: { uint32_t v;memcpy(&v,(void*)addr,4); *out=v; break; }
        case T_I64:
        case T_U64:
        case T_PTR: { uint64_t v; memcpy(&v,(void*)addr,8); *out=v; break; }
        case T_F32: { float v; memcpy(&v,(void*)addr,4); uint32_t u; memcpy(&u,&v,4); *out=u; break; }
        case T_F64: { double v; memcpy(&v,(void*)addr,8); uint64_t u; memcpy(&u,&v,8); *out=u; break; }
    }
}

static void hook_generic(int idx, void* a0, void* a1, void* a2, void* a3) {
    HookSlot& h = g_hooks[idx];

    // 抓帧
    uint32_t wi = h.write_idx;
    HookRecord& rec = h.records[wi & 0xFF];
    uint64_t args[5] = { (uint64_t)a0, (uint64_t)a1, (uint64_t)a2, (uint64_t)a3, 0 };
    for (int i = 0; i < h.field_count; ++i) {
        SpecItem& it = h.fields[i];
        uint64_t base = (it.src == 0) ? (uint64_t)a0 : args[it.src];
        uint64_t v = 0;
        if (sigsetjmp(g_dump_jmp, 1) == 0) read_field(base + it.off, it, &v);
        rec.vals[i] = v;
    }
    h.write_idx = wi + 1;
    __atomic_add_fetch((int*)&h.hit_count, 1, __ATOMIC_RELAXED);

    // 恢复原码 → 调原函数 → 重打 patch
    pthread_mutex_lock(&h.lock);
    uint64_t t = h.target;
    uint64_t page = t & ~0xFFFULL;
    mprotect((void*)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy((void*)t, h.saved, 16);
    __builtin___clear_cache((char*)t, (char*)(t + 16));
    ((void(*)(void*,void*,void*,void*))t)(a0, a1, a2, a3);
    write_jump(t, (uint64_t)g_entry_table[idx]);
    pthread_mutex_unlock(&h.lock);
}

static int hook_install_ex(uint64_t target, const char* name, const char* spec) {
    for (int i = 0; i < 4; ++i) {
        if (g_hooks[i].active) continue;
        HookSlot& h = g_hooks[i];
        uint64_t page = target & ~0xFFFULL;
        if (mprotect((void*)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
            return -2;
        memcpy(h.saved, (void*)target, 16);
        h.target = target;
        snprintf(h.name, sizeof(h.name), "%s", name ? name : "?");
        snprintf(h.spec, sizeof(h.spec), "%s", spec ? spec : "this:i64");
        if (!parse_spec(h, spec ? spec : "this:i64")) return -3;
        h.hit_count = 0;
        h.write_idx = 0;
        pthread_mutex_init(&h.lock, nullptr);
        write_jump(target, (uint64_t)g_entry_table[i]);
        h.active = true;
        return i;
    }
    return -1;
}

static int hook_remove(int idx) {
    if (idx < 0 || idx >= 4) return -1;
    HookSlot& h = g_hooks[idx];
    if (!h.active) return -1;
    uint64_t page = h.target & ~0xFFFULL;
    mprotect((void*)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy((void*)h.target, h.saved, 16);
    __builtin___clear_cache((char*)h.target, (char*)(h.target + 16));
    h.active = false;
    return 0;
}

// ===========================================================================
// RPC server
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

static HealthScanApi g_rpc_api;
static bool g_rpc_api_ok = false;

static void *rpc_find_class(const char *className, void **outImage = nullptr) {
    if (outImage) *outImage = nullptr;
    if (!className || !*className) return nullptr;
    void *domain = g_rpc_api.domain_get();
    if (!domain) return nullptr;
    size_t asmCount = 0;
    void **assemblies = (void **)g_rpc_api.domain_get_assemblies(domain, &asmCount);
    if (!assemblies) return nullptr;
    for (size_t i = 0; i < asmCount; ++i) {
        void *image = g_rpc_api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        size_t classCount = g_rpc_api.image_get_class_count(image);
        for (size_t j = 0; j < classCount; ++j) {
            void *klass = g_rpc_api.image_get_class(image, j);
            if (!klass) continue;
            const char *name = g_rpc_api.class_get_name(klass);
            const char *ns = g_rpc_api.class_get_namespace(klass);
            char full[512] = {0};
            snprintf(full, sizeof(full), "%s.%s", ns ? ns : "", name ? name : "");
            if (strcmp(className, name ? name : "") == 0 || strcmp(className, full) == 0) {
                if (outImage) *outImage = image;
                return klass;
            }
        }
    }
    return nullptr;
}

static bool rpc_field_offset(void *klass, const char *fieldName, size_t *outOffset) {
    if (!klass || !fieldName || !outOffset) return false;
    void *iter = nullptr;
    while (auto field = g_rpc_api.class_get_fields(klass, &iter)) {
        const char *name = g_rpc_api.field_get_name(field);
        if (name && strcmp(name, fieldName) == 0) {
            *outOffset = g_rpc_api.field_get_offset(field);
            return true;
        }
    }
    return false;
}

static bool rpc_scan_rw_instances(FILE *out, void *klass, int limit) {
    if (!klass) return false;
    if (limit <= 0 || limit > 256) limit = 32;

    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        fprintf(out, "ERR: cannot open /proc/self/maps\n");
        return false;
    }

    // This scan is read-only. Mappings can disappear during Unity GC, so use
    // the existing SIGSEGV/SIGBUS guard for the RPC thread.
    install_dump_guard();
    g_dump_guard_active = 1;

    uint64_t target = (uint64_t)(uintptr_t)klass;
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), maps) && found < limit) {
        uint64_t start = 0, end = 0;
        char perms[8] = {0};
        char path[512] = {0};
        int n = sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s %*s %*s %*s %511[^\n]",
                       &start, &end, perms, path);
        if (n < 3 || end <= start) continue;
        // Managed objects normally live in writable private mappings.
        if (perms[0] != 'r' || perms[1] != 'w' || perms[3] != 'p') continue;
        if (n >= 4 && (strstr(path, "[stack") || strstr(path, "[vdso") || strstr(path, "[vvar"))) continue;
        uint64_t size = end - start;
        if (size == 0 || size > 512ull * 1024ull * 1024ull) continue;

        const size_t chunkSize = 1u << 20;
        for (uint64_t off = 0; off + sizeof(uint64_t) <= size && found < limit; ) {
            size_t want = (size_t)std::min<uint64_t>(chunkSize, size - off);
            if (sigsetjmp(g_dump_jmp, 1) != 0) break;
            const uint8_t *buf = (const uint8_t *)(start + off);
            for (size_t i = 0; i + sizeof(uint64_t) <= want && found < limit; i += sizeof(uint64_t)) {
                uint64_t v = 0;
                memcpy(&v, buf + i, sizeof(v));
                if (v != target) continue;
                uint64_t object = start + off + i;
                fprintf(out, "instance 0x%" PRIx64 " klass=0x%" PRIx64 "\n", object, target);
                ++found;
            }
            if (want < chunkSize) break;
            off += want - sizeof(uint64_t);
            off &= ~(uint64_t)(sizeof(uint64_t) - 1);
        }
    }
    fclose(maps);
    g_dump_guard_active = 0;
    remove_dump_guard();
    fprintf(out, "instances=%d\n", found);
    return found > 0;
}

struct RpcLivenessResult {
    void *filter;
    int limit;
    std::vector<uint64_t> objects;
};

static void rpc_liveness_object_callback(
        Il2CppObject **objects, int size, void *userdata) {
    auto *result = (RpcLivenessResult *)userdata;
    if (!result || !objects || size <= 0) return;
    for (int i = 0; i < size && (int)result->objects.size() < result->limit; ++i) {
        Il2CppObject *object = objects[i];
        if (!object || object->klass != result->filter) continue;
        uint64_t addr = (uint64_t)(uintptr_t)object;
        if (std::find(result->objects.begin(), result->objects.end(), addr)
                == result->objects.end()) {
            result->objects.push_back(addr);
        }
    }
}

static void *rpc_liveness_reallocate(
        void *ptr, size_t size, void *userdata) {
    (void)userdata;
    return realloc(ptr, size);
}

static bool rpc_live_instances(FILE *out, void *klass, int limit) {
    if (!klass) return false;
    if (limit <= 0 || limit > 256) limit = 32;
    if (!il2cpp_unity_liveness_allocate_struct ||
        !il2cpp_unity_liveness_calculation_from_statics ||
        !il2cpp_unity_liveness_finalize ||
        !il2cpp_unity_liveness_free_struct) {
        fprintf(out, "ERR: IL2CPP liveness API unavailable\n");
        return false;
    }

    Il2CppThread *attached = nullptr;
    if (il2cpp_thread_current && il2cpp_thread_attach &&
        !il2cpp_thread_current()) {
        attached = il2cpp_thread_attach(
                (Il2CppDomain *)g_rpc_api.domain_get());
    }

    RpcLivenessResult result{};
    result.filter = klass;
    result.limit = limit;
    void *state = il2cpp_unity_liveness_allocate_struct(
            (Il2CppClass *)klass,
            limit,
            rpc_liveness_object_callback,
            &result,
            rpc_liveness_reallocate);
    if (!state) {
        if (attached && il2cpp_thread_detach) il2cpp_thread_detach(attached);
        fprintf(out, "ERR: liveness state allocation failed\n");
        return false;
    }

    il2cpp_unity_liveness_calculation_from_statics(state);
    il2cpp_unity_liveness_finalize(state);
    il2cpp_unity_liveness_free_struct(state);

    if (attached && il2cpp_thread_detach) il2cpp_thread_detach(attached);

    for (uint64_t object : result.objects) {
        fprintf(out, "instance 0x%" PRIx64 " klass=0x%" PRIx64 "\n",
                object, (uint64_t)(uintptr_t)klass);
    }
    fprintf(out, "instances=%zu source=il2cpp_liveness\n",
            result.objects.size());
    return !result.objects.empty();
}

static bool rpc_health_dump(FILE *out, uint64_t service, int limit) {
    if (limit <= 0 || limit > 4096) limit = 512;
    if (service < 0x100000000ULL || service > 0x800000000000ULL) {
        fprintf(out, "ERR: invalid HealthLinkService address\n");
        return false;
    }

    install_dump_guard();
    g_dump_guard_active = 1;
    if (sigsetjmp(g_dump_jmp, 1) != 0) {
        g_dump_guard_active = 0;
        remove_dump_guard();
        fprintf(out, "ERR: stale/unreadable address while walking Health pool\n");
        return false;
    }

    // HealthLinkService fields from the runtime class layout:
    //   +0x10 EcsWorld* _defaultWorld
    //   +0x28 EcsPool<Health>* _healthPool
    uint64_t world = 0;
    uint64_t pool = 0;
    memcpy(&world, (void *)(service + 0x10), sizeof(world));
    memcpy(&pool, (void *)(service + 0x28), sizeof(pool));
    if (pool < 0x100000000ULL || pool > 0x800000000000ULL) {
        g_dump_guard_active = 0;
        remove_dump_guard();
        fprintf(out, "ERR: service+0x28 is not an EcsPool pointer: 0x%" PRIx64 "\n", pool);
        return false;
    }

    // Resolve the concrete inflated generic class from the live object's
    // klass pointer. The generic definition reported by `class EcsPool` can
    // expose zero offsets, while the inflated runtime class has real offsets.
    uint64_t poolKlass = 0;
    memcpy(&poolKlass, (void *)pool, sizeof(poolKlass));
    size_t denseOff = 0x38;
    size_t sparseOff = 0x40;
    size_t countOff = 0x48;
    bool dynamicLayout = false;
    if (poolKlass >= 0x100000000ULL && poolKlass <= 0x800000000000ULL) {
        size_t d = 0, s = 0, c = 0;
        if (rpc_field_offset((void *)(uintptr_t)poolKlass, "_denseItems", &d) &&
            rpc_field_offset((void *)(uintptr_t)poolKlass, "_sparseItems", &s) &&
            rpc_field_offset((void *)(uintptr_t)poolKlass, "_denseItemsCount", &c) &&
            d < 0x200 && s < 0x200 && c < 0x200) {
            denseOff = d;
            sparseOff = s;
            countOff = c;
            dynamicLayout = true;
        }
    }

    uint64_t dense = 0;
    uint64_t sparse = 0;
    int32_t denseCount = 0;
    memcpy(&dense, (void *)(pool + denseOff), sizeof(dense));
    memcpy(&sparse, (void *)(pool + sparseOff), sizeof(sparse));
    memcpy(&denseCount, (void *)(pool + countOff), sizeof(denseCount));
    if (dense < 0x100000000ULL || dense > 0x800000000000ULL ||
        sparse < 0x100000000ULL || sparse > 0x800000000000ULL ||
        denseCount < 0 || denseCount > 1000000) {
        g_dump_guard_active = 0;
        remove_dump_guard();
        fprintf(out, "ERR: unexpected EcsPool layout pool=0x%" PRIx64
                     " klass=0x%" PRIx64 " denseOff=0x%zx sparseOff=0x%zx countOff=0x%zx"
                     " dense=0x%" PRIx64 " sparse=0x%" PRIx64 " count=%d\n",
                pool, poolKlass, denseOff, sparseOff, countOff, dense, sparse, denseCount);
        return false;
    }

    uint64_t sparseLen = 0;
    // Il2CppArray::max_length is at +0x18 on this arm64 runtime.
    memcpy(&sparseLen, (void *)(sparse + 0x18), sizeof(sparseLen));
    if (sparseLen > 1000000) sparseLen = 1000000;
    fprintf(out, "world=0x%" PRIx64 " pool=0x%" PRIx64
                 " dense=0x%" PRIx64 " sparse=0x%" PRIx64
                 " denseCount=%d sparseLen=%" PRIu64
                 " layout=%s offsets=(0x%zx,0x%zx,0x%zx)\n",
            world, pool, dense, sparse, denseCount, sparseLen,
            dynamicLayout ? "runtime" : "fallback", denseOff, sparseOff, countOff);

    int shown = 0;
    for (uint64_t entity = 0; entity < sparseLen && shown < limit; ++entity) {
        int32_t denseIndex = -1;
        memcpy(&denseIndex, (void *)(sparse + 0x20 + entity * sizeof(int32_t)), sizeof(denseIndex));
        // LeoEcsLite reserves dense slot 0 as the "component absent" sentinel.
        // Real components occupy slots [1, denseCount).
        if (denseIndex <= 0 || denseIndex >= denseCount) continue;

        uint64_t item = dense + 0x20 + (uint64_t)denseIndex * 0x18;
        int64_t value = 0;
        int64_t maxValue = 0;
        memcpy(&value, (void *)item, sizeof(value));
        memcpy(&maxValue, (void *)(item + 0x08), sizeof(maxValue));
        double ratio = maxValue > 0 ? (double)value * 100.0 / (double)maxValue : 0.0;
        fprintf(out, "entity=%" PRIu64 " dense=%d value=%" PRId64
                     " max=%" PRId64 " ratio=%.2f%%\n",
                entity, denseIndex, value, maxValue, ratio);
        ++shown;
    }

    g_dump_guard_active = 0;
    remove_dump_guard();
    fprintf(out, "health_rows=%d\n", shown);
    return true;
}

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

static void rpc_dump_methods(FILE *out, const char *cls) {
    void *domain = g_rpc_api.domain_get();
    if (!domain) { fprintf(out, "ERR: no domain\n"); return; }
    size_t asmCount = 0;
    void **assemblies = (void **)g_rpc_api.domain_get_assemblies(domain, &asmCount);
    int found = 0;
    for (size_t i = 0; i < asmCount; ++i) {
        void *image = g_rpc_api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        const char *imageName = g_rpc_api.image_get_name ? g_rpc_api.image_get_name(image) : "?";
        size_t classCount = g_rpc_api.image_get_class_count(image);
        for (size_t j = 0; j < classCount; ++j) {
            void *klass_v = g_rpc_api.image_get_class(image, j);
            if (!klass_v) continue;
            Il2CppClass *klass = (Il2CppClass *)klass_v;
            const char *name = g_rpc_api.class_get_name(klass_v);
            const char *ns   = g_rpc_api.class_get_namespace(klass_v);
            if (!name) continue;
            char full[512];
            snprintf(full, sizeof(full), "%s.%s", ns ? ns : "", name);
            if (strcmp(full, cls) != 0 && strcmp(name, cls) != 0) continue;
            found++;
            fprintf(out, "\n=== %s (image=%s) ===\n", full, imageName ? imageName : "?");
            void *iter = nullptr;
            while (auto method = il2cpp_class_get_methods(klass, &iter)) {
                if (!method) continue;
                const char *mname = il2cpp_method_get_name(method);
                uint32_t iflags = 0;
                uint32_t mflags = il2cpp_method_get_flags(method, &iflags);
                uint32_t pc = il2cpp_method_get_param_count(method);
                uint64_t va = (uint64_t)method->methodPointer;
                uint64_t rva = va ? va - il2cpp_base : 0;
                fprintf(out, "  VA=0x%016" PRIx64 " RVA=0x%08" PRIx64 " %s%s(",
                        va, rva,
                        (mflags & METHOD_ATTRIBUTE_STATIC) ? "static " : "",
                        mname ? mname : "?");
                for (uint32_t p = 0; p < pc; ++p) {
                    auto param = il2cpp_method_get_param(method, p);
                    auto pclass = il2cpp_class_from_type(param);
                    fprintf(out, "%s%s", p ? ", " : "", pclass ? il2cpp_class_get_name(pclass) : "?");
                }
                fprintf(out, ")\n");
            }
        }
    }
    fprintf(out, "\n--- found %d ---\n", found);
}
static void rpc_list_static(FILE *out, const char *cls) {
    void *domain = g_rpc_api.domain_get();
    if (!domain) return;
    size_t asmCount = 0;
    void **assemblies = (void **)g_rpc_api.domain_get_assemblies(domain, &asmCount);
    int found = 0;
    for (size_t i = 0; i < asmCount; ++i) {
        void *image = g_rpc_api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        const char *imageName = g_rpc_api.image_get_name ? g_rpc_api.image_get_name(image) : "?";
        size_t classCount = g_rpc_api.image_get_class_count(image);
        for (size_t j = 0; j < classCount; ++j) {
            void *klass_v = g_rpc_api.image_get_class(image, j);
            if (!klass_v) continue;
            Il2CppClass *klass = (Il2CppClass *)klass_v;
            const char *name = g_rpc_api.class_get_name(klass_v);
            const char *ns   = g_rpc_api.class_get_namespace(klass_v);
            if (!name) continue;
            char full[512];
            snprintf(full, sizeof(full), "%s.%s", ns ? ns : "", name);
            if (strcmp(full, cls) != 0 && strcmp(name, cls) != 0) continue;
            found++;
            fprintf(out, "=== %s (image=%s) ===\n", full, imageName ? imageName : "?");
            void *fiter = nullptr;
            while (auto field = il2cpp_class_get_fields(klass, &fiter)) {
                auto attrs = il2cpp_field_get_flags(field);
                if (!(attrs & FIELD_ATTRIBUTE_STATIC)) continue;
                const char *fn = g_rpc_api.field_get_name(field);
                fprintf(out, "  [S] %s\n", fn ? fn : "?");
            }
        }
    }
    fprintf(out, "\n--- found %d ---\n", found);
}

static void rpc_get_static(FILE *out, const char *cls, const char *fldname) {
    void *domain = g_rpc_api.domain_get();
    if (!domain) return;
    size_t asmCount = 0;
    void **assemblies = (void **)g_rpc_api.domain_get_assemblies(domain, &asmCount);
    int found = 0;
    for (size_t i = 0; i < asmCount; ++i) {
        void *image = g_rpc_api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        size_t classCount = g_rpc_api.image_get_class_count(image);
        for (size_t j = 0; j < classCount; ++j) {
            void *klass_v = g_rpc_api.image_get_class(image, j);
            if (!klass_v) continue;
            Il2CppClass *klass = (Il2CppClass *)klass_v;
            const char *name = g_rpc_api.class_get_name(klass_v);
            const char *ns   = g_rpc_api.class_get_namespace(klass_v);
            if (!name) continue;
            char full[512];
            snprintf(full, sizeof(full), "%s.%s", ns ? ns : "", name);
            if (strcmp(full, cls) != 0 && strcmp(name, cls) != 0) continue;

            void *fiter = nullptr;
            while (auto field = il2cpp_class_get_fields(klass, &fiter)) {
                const char *fn = g_rpc_api.field_get_name(field);
                if (!fn || strcmp(fn, fldname) != 0) continue;
                auto attrs = il2cpp_field_get_flags(field);
                if (!(attrs & FIELD_ATTRIBUTE_STATIC)) {
                    fprintf(out, "  %s.%s: not static\n", full, fldname);
                    continue;
                }
                uint64_t val = 0;
                il2cpp_field_static_get_value(field, &val);
                found++;
                fprintf(out, "  %s.%s = 0x%" PRIx64 "\n", full, fldname, val);
            }
        }
    }
    fprintf(out, "\n--- found %d ---\n", found);
}
static int rpc_scan(FILE *out, const char *imagePattern, const char *classPattern, bool dumpAllInMatchedImage) {
    void *domain = g_rpc_api.domain_get();
    if (!domain) return -1;
    size_t asmCount = 0;
    void **assemblies = (void **)g_rpc_api.domain_get_assemblies(domain, &asmCount);
    if (!assemblies) return -1;
    fprintf(out, "assemblies count = %zu\n", asmCount);
    int hits = 0;
    for (size_t i = 0; i < asmCount; ++i) {
        void *image = g_rpc_api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        const char *imageName = g_rpc_api.image_get_name ? g_rpc_api.image_get_name(image) : "?";
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
        if (sscanf(cmd + 10, "%127s", img) == 1) rpc_scan(out, img, nullptr, true);
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
    else if (strncmp(cmd, "instances ", 10) == 0) {
        fprintf(out, "ERR: instances disabled; use liveinstances <ClassName> [limit]\n");
    }
    else if (strncmp(cmd, "liveinstances ", 14) == 0) {
        char cls[256] = {0};
        int limit = 32;
        int n = sscanf(cmd + 14, "%255s %d", cls, &limit);
        if (n >= 1) {
            void *klass = rpc_find_class(cls);
            if (!klass) {
                fprintf(out, "ERR: class not found: %s\n", cls);
            } else {
                fprintf(out, "class=%s klass=0x%" PRIx64 "\n", cls, (uint64_t)(uintptr_t)klass);
                rpc_live_instances(out, klass, limit);
            }
        } else {
            fprintf(out, "ERR: usage: liveinstances <ClassName> [limit]\n");
        }
    }
    else if (strncmp(cmd, "healthdump ", 11) == 0) {
        uint64_t service = 0;
        int limit = 512;
        int n = sscanf(cmd + 11, "%" SCNx64 " %d", &service, &limit);
        if (n >= 1) {
            rpc_health_dump(out, service, limit);
        } else {
            fprintf(out, "ERR: usage: healthdump <HealthLinkService_addr> [limit]\n");
        }
    }
    else if (strncmp(cmd, "methods ", 8) == 0) {
        char cls[256] = {0};
        if (sscanf(cmd + 8, "%255s", cls) == 1) rpc_dump_methods(out, cls);
        else fprintf(out, "ERR: usage: methods <ClassName>\n");
    }
    else if (strncmp(cmd, "liststatic ", 11) == 0) {
        char cls[256] = {0};
        if (sscanf(cmd + 11, "%255s", cls) == 1) rpc_list_static(out, cls);
        else fprintf(out, "ERR: usage: liststatic <ClassName>\n");
    }
    else if (strncmp(cmd, "getstatic ", 10) == 0) {
        char cls[256] = {0}, fld[128] = {0};
        if (sscanf(cmd + 10, "%255s %127s", cls, fld) == 2) rpc_get_static(out, cls, fld);
        else fprintf(out, "ERR: usage: getstatic <ClassName> <FieldName>\n");
    }
    else if (strncmp(cmd, "hook ", 5) == 0) {
        uint64_t addr = 0;
        char rest[256] = {0};
        if (sscanf(cmd + 5, "%" SCNx64 " %255[^\n]", &addr, rest) >= 1) {
            char name[64] = {0};
            char spec[128] = {0};
            char* sp = rest;
            while (*sp == ' ') sp++;
            char* sep = strchr(sp, ' ');
            if (sep) {
                *sep = 0;
                strncpy(name, sp, 63);
                strncpy(spec, sep + 1, 127);
            } else {
                strncpy(name, sp, 63);
                strcpy(spec, "this:i64");
            }
            int idx = hook_install_ex(addr, name, spec);
            if (idx >= 0)
                fprintf(out, "hook installed: slot=%d addr=0x%" PRIx64 " spec=%s\n", idx, addr, spec);
            else
                fprintf(out, "hook failed: %d\n", idx);
        } else {
            fprintf(out, "ERR: usage: hook <addr> [name] [spec]\n");
        }
    }
    else if (strncmp(cmd, "unhook ", 7) == 0) {
        int idx = -1;
        if (sscanf(cmd + 7, "%d", &idx) == 1) {
            int r = hook_remove(idx);
            fprintf(out, "unhook slot=%d result=%d\n", idx, r);
        }
    }
    else if (strcmp(cmd, "unhookall") == 0) {
        for (int i = 0; i < 4; ++i) if (g_hooks[i].active) hook_remove(i);
        fprintf(out, "all unhooked\n");
    }
    else if (strncmp(cmd, "hits", 4) == 0 && (cmd[4] == 0 || cmd[4] == ' ')) {
        int slot = -1, n = 20;
        if (strlen(cmd) > 5) sscanf(cmd + 5, "%d %d", &slot, &n);
        if (slot < 0 || slot >= 4 || !g_hooks[slot].active) {
            fprintf(out, "ERR: no such hook (usage: hits <slot> [n])\n");
        } else {
            HookSlot& h = g_hooks[slot];
            uint32_t total = h.hit_count;
            uint32_t cnt = (n > 0 && (uint32_t)n < total) ? n : total;
            if (cnt > 256) cnt = 256;
            uint32_t end = h.write_idx;
            uint32_t start = end - cnt;
            fprintf(out, "[%d] %s hits=%u shown=%u spec=%s\n",
                    slot, h.name, total, cnt, h.spec);
            for (uint32_t k = 0; k < cnt; ++k) {
                HookRecord& rec = h.records[(start + k) & 0xFF];
                fprintf(out, "  #%u:", start + k);
                for (int i = 0; i < h.field_count; ++i) {
                    SpecItem& it = h.fields[i];
                    uint64_t v = rec.vals[i];
                    fprintf(out, " ");
                    switch (it.type) {
                        case T_I8:  fprintf(out, "%d", (int)(int8_t)v); break;
                        case T_U8:  fprintf(out, "%u", (unsigned)(uint8_t)v); break;
                        case T_I16: fprintf(out, "%d", (int)(int16_t)v); break;
                        case T_U16: fprintf(out, "%u", (unsigned)(uint16_t)v); break;
                        case T_I32: fprintf(out, "%d", (int)(int32_t)v); break;
                        case T_U32: fprintf(out, "%u", (unsigned)(uint32_t)v); break;
                        case T_I64: fprintf(out, "%" PRId64, (int64_t)v); break;
                        case T_U64:
                        case T_PTR: fprintf(out, "0x%" PRIx64, v); break;
                        case T_F32: { float f; memcpy(&f,&v,4); fprintf(out, "%g", f); break; }
                        case T_F64: { double d; memcpy(&d,&v,8); fprintf(out, "%g", d); break; }
                    }
                }
                fprintf(out, "\n");
            }
        }
    }
    else if (strncmp(cmd, "read ", 5) == 0) {
        uint64_t addr = 0; int size = 0;
        if (sscanf(cmd + 5, "%" SCNx64 " %d", &addr, &size) == 2 && size > 0 && size <= 4096) {
            uint8_t *p = (uint8_t *)addr;
            for (int i = 0; i < size; i += 16) {
                fprintf(out, "%016" PRIx64 "  ", addr + i);
                for (int k = 0; k < 16 && i + k < size; ++k) fprintf(out, "%02x ", p[i + k]);
                fprintf(out, " |");
                for (int k = 0; k < 16 && i + k < size; ++k) {
                    char c = p[i + k];
                    fprintf(out, "%c", (c >= 0x20 && c < 0x7f) ? c : '.');
                }
                fprintf(out, "|\n");
            }
        } else fprintf(out, "ERR: usage: read <addr_hex> <size>\n");
    }
    else if (strncmp(cmd, "write ", 6) == 0) {
        uint64_t addr = 0; char hex[1024] = {0};
        if (sscanf(cmd + 6, "%" SCNx64 " %1023s", &addr, hex) == 2) {
            size_t hlen = strlen(hex);
            if (hlen % 2 != 0) fprintf(out, "ERR: hex len odd\n");
            else {
                uint8_t *p = (uint8_t *)addr;
                for (size_t i = 0; i < hlen; i += 2) {
                    unsigned v; sscanf(hex + i, "%2x", &v);
                    p[i / 2] = (uint8_t)v;
                }
                fprintf(out, "ok, wrote %zu bytes\n", hlen / 2);
            }
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
    else if (strncmp(cmd, "readu64 ", 8) == 0) {
        uint64_t addr = 0;
        if (sscanf(cmd + 8, "%" SCNx64, &addr) == 1) {
            uint64_t v; memcpy(&v, (void*)addr, 8);
            fprintf(out, "uint64 @0x%" PRIx64 " = 0x%" PRIx64 "\n", addr, v);
        }
    }
    else if (strncmp(cmd, "readstr ", 8) == 0) {
        uint64_t addr = 0;
        int maxLen = 256;
        int n = sscanf(cmd + 8, "%" SCNx64 " %d", &addr, &maxLen);
        if (n >= 1) {
            if (maxLen <= 0 || maxLen > 4096) maxLen = 256;
            const char *p = (const char *)addr;
            int len = 0;
            for (; len < maxLen; ++len) {
                if (p[len] == '\0') break;
            }
            fprintf(out, "str @0x%" PRIx64 " = \"", addr);
            for (int i = 0; i < len; ++i) {
                unsigned char c = (unsigned char)p[i];
                if (c == '\\' || c == '"') fprintf(out, "\\%c", c);
                else if (c >= 0x20 && c < 0x7f) fputc(c, out);
                else fprintf(out, "\\x%02x", c);
            }
            fprintf(out, "\"\n");
        }
    }
    else {
        fprintf(out, "ERR: unknown cmd\n");
        fprintf(out, "cmds: ping | base | images | scan <img> <cls> | dumpimage <img>\n"
                     "      class <name> | liveinstances <name> [limit] | healthdump <addr> [limit] | methods <name>\n"
                     "      hook <addr> [name] [spec] | unhook <idx> | unhookall | hits <slot> [n]\n"
                     "      read <addr> <sz> | write <addr> <hex> | readf <addr> | readi <addr>\n"
                     "      readu64 <addr> | readstr <addr> [max]\n"
                     "spec: src[:type][,src[:type]]*\n"
                     "  src  = this | a0 | a1 | a2 | a3 (支持 +offset, 如 this+0x28)\n"
                     "  type = i8|u8|i16|u16|i32|u32|i64|u64|f32|f64|ptr (默认 i64)\n"
                     "例: hook 0x7a879894c DamageSystem.Run this:i64,this+0x28:ptr,this+0x68:ptr\n");
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
    if (srv < 0) return nullptr;
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
            if (!out) goto cli_done;
            rpc_handle(line, out);
            fclose(out);
            if (buf) { write(cli, buf, buflen); free(buf); }
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
        if (!fp) break;
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
            if (sigsetjmp(g_dump_jmp, 1) != 0) continue;
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

                LOGI("HIT @ %" PRIx64 " ver=%u sl=0x%x (try %d)", r.start + off, version, sl, attempt);

                uint64_t remaining = sz - off;
                uint64_t dumpSize = remaining > (128ull * 1024 * 1024) ? (128ull * 1024 * 1024) : remaining;
                if (sigsetjmp(g_dump_jmp, 1) != 0) continue;
                FILE *out = fopen(path, "wb");
                if (!out) continue;
                size_t w = fwrite(base + off, 1, dumpSize, out);
                fclose(out);
                LOGI("dumped %zu bytes -> %s", w, path);

                dump_il2cpp_so(outDir);
                g_dump_guard_active = 0;
                remove_dump_guard();

                if (g_il2cpp_handle) {
                    pthread_t th;
                    if (pthread_create(&th, nullptr, rpc_server_thread, nullptr) == 0) {
                        pthread_detach(th);
                        LOGI("rpc server thread started");
                    }
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
