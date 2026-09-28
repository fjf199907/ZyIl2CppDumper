// Included after the checked read and metadata helpers in il2cpp_dump.cpp.
// Never retain a managed instance for use by a later callback or RPC thread.
#include "dobby.h"
#include <chrono>
#include <deque>
#include <mutex>
#include <cstdarg>

namespace health_hook {
static std::mutex mutex;
static bool installed = false, enabled = false;
static uint64_t hook_address = 0;
static uint64_t deadline = 0, next_scan = 0, interval_ms = 250;
static int64_t target = 1;
enum class Mode { Count, Read, Write };
static Mode mode = Mode::Write;
static const char *mode_name() {
    return mode == Mode::Count ? "count" : mode == Mode::Read ? "read" : "write";
}
static uint64_t callbacks = 0, scans = 0, writes = 0, dropped = 0;
static uint64_t last_startup = 0, last_hash = UINT64_MAX;
static void *startup_class = nullptr, *character_class = nullptr, *health_system_class = nullptr;
static void *damage_class = nullptr;
static std::deque<std::string> events;
static std::string last_error;
static uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void event(const char *fmt, ...) {
    char text[512]; va_list args; va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args); va_end(args);
    if (events.size() == 256) { events.pop_front(); ++dropped; }
    events.emplace_back(text);
}
static bool fail(const char *message) {
    if (last_error != message) { event("poll: %s", message); last_error = message; }
    return false;
}
static bool pointer(uint64_t location, uint64_t &result) {
    return rpc_read_pointer(location, &result) && result >= 0x100000000ull &&
        result < 0x800000000000ull && !(result & 7);
}
static bool is_class(uint64_t object, void *expected) {
    uint64_t actual = 0;
    return rpc_read_pointer(object, &actual) && actual == (uint64_t)expected;
}
struct Pool {
    uint64_t object = 0, world = 0, dense = 0, sparse = 0, capacity = 0, length = 0;
    int32_t count = 0;
};
static bool pool(uint64_t object, Pool &p) {
    p.object = object;
    return pointer(object + 0x18, p.world) && pointer(object + 0x38, p.dense) &&
        pointer(object + 0x40, p.sparse) &&
        rpc_read_exact(object + 0x48, &p.count, 4) &&
        rpc_read_exact(p.dense + 0x18, &p.capacity, 8) &&
        rpc_read_exact(p.sparse + 0x18, &p.length, 8) &&
        p.count >= 1 && (uint64_t)p.count <= p.capacity && p.capacity <= 65536 &&
        p.length > 0 && p.length <= 65536;
}
static bool unchanged(const Pool &p) {
    Pool q;
    return pool(p.object, q) && q.world == p.world && q.dense == p.dense &&
        q.sparse == p.sparse && q.count == p.count && q.capacity == p.capacity && q.length == p.length;
}
static bool scan(uint64_t startup) {
    uint64_t view, list, top, group, child, character, health_system, hp, ep, capacity;
    int32_t count;
    if (!is_class(startup, startup_class) || !pointer(startup + 0x38, view) ||
        !pointer(view + 0x20, list) || !pointer(list + 0x10, top) ||
        !rpc_read_exact(list + 0x18, &count, 4) ||
        !rpc_read_exact(top + 0x18, &capacity, 8) || count < 1 ||
        (uint64_t)count > capacity || capacity > 65536 ||
        !pointer(top + 0x20, group) || !pointer(group + 0x10, child) ||
        !rpc_read_exact(child + 0x18, &capacity, 8) || capacity < 16 || capacity > 65536 ||
        !pointer(child + 0x20 + 14 * 8, character) ||
        !pointer(child + 0x20 + 15 * 8, health_system)) return fail("current TickView system chain invalid");
    if (!is_class(character, character_class) || !is_class(health_system, health_system_class))
        return fail("child indices 14/15 type mismatch; no writes");
    if (!pointer(health_system + 0x50, hp) || !pointer(character + 0x120, ep))
        return fail("current pool pointer invalid");
    Pool health, enemy;
    if (!pool(hp, health) || !pool(ep, enemy) || health.world != enemy.world)
        return fail("current pool headers/world invalid");
    std::vector<int32_t> hs(health.length), es(enemy.length);
    if (!rpc_read_exact(health.sparse + 0x20, hs.data(), hs.size() * 4) ||
        !rpc_read_exact(enemy.sparse + 0x20, es.data(), es.size() * 4))
        return fail("current sparse arrays unreadable");
    if (!unchanged(health) || !unchanged(enemy)) return fail("pool changed during scan");
    if (last_startup != startup) {
        event("address chain (current TickView this):");
        event("  startup=0x%" PRIx64, startup);
        event("  startup+0x38 -> viewSystems=0x%" PRIx64, view);
        event("  viewSystems+0x20 -> systemsList=0x%" PRIx64, list);
        event("  systemsList+0x10 -> topSystemsArray=0x%" PRIx64, top);
        event("  topSystemsArray+0x20 -> group0=0x%" PRIx64, group);
        event("  group0+0x10 -> childArray=0x%" PRIx64, child);
        event("  childArray+0x90 -> BindCharacterSystem=0x%" PRIx64, character);
        event("  childArray+0x98 -> BindCharacterInfoViewSystem=0x%" PRIx64, health_system);
        event("  BindCharacterInfoViewSystem+0x50 -> HealthPool=0x%" PRIx64, hp);
        event("  BindCharacterSystem+0x120 -> EnemyPool=0x%" PRIx64, ep);
        last_startup = startup;
    }
    unsigned rows = 0;
    uint64_t hash = hp ^ ep;
    for (size_t entity = 0; entity < std::min(hs.size(), es.size()); ++entity) {
        const int32_t slot = hs[entity], enemy_slot = es[entity];
        if (slot <= 0 || slot >= health.count || enemy_slot <= 0 || enemy_slot >= enemy.count) continue;
        ++rows; hash = (hash ^ entity) * 1099511628211ull;
        const uint64_t address = health.dense + 0x20 + (uint64_t)slot * 0x18;
        int64_t values[2];
        if (!rpc_read_exact(address, values, sizeof(values))) return fail("Health value unreadable");
        if (mode != Mode::Write) continue;
        // Do not resurrect dead entities or touch uninitialised components.
        if (values[0] <= 0 || values[1] <= 0 || target > values[1] || values[0] == target) continue;
        int32_t current_hs = 0, current_es = 0;
        uint64_t current_hp = 0, current_ep = 0;
        if (!unchanged(health) || !unchanged(enemy) ||
            !rpc_read_pointer(health_system + 0x50, &current_hp) || current_hp != hp ||
            !rpc_read_pointer(character + 0x120, &current_ep) || current_ep != ep ||
            !rpc_read_exact(health.sparse + 0x20 + entity * 4, &current_hs, 4) || current_hs != slot ||
            !rpc_read_exact(enemy.sparse + 0x20 + entity * 4, &current_es, 4) || current_es != enemy_slot)
            return fail("mapping changed before write");
        iovec local{&target, sizeof(target)};
        iovec remote{reinterpret_cast<void *>(address), sizeof(target)};
        if (process_vm_writev(getpid(), &local, 1, &remote, 1, 0) != sizeof(target))
            return fail("checked health write failed");
        int64_t actual;
        if (!rpc_read_exact(address, &actual, 8) || actual != target) return fail("health write verification failed");
        ++writes;
        event("updated entity=%zu slot=%d address=0x%" PRIx64 " value=%" PRId64, entity, slot, address, actual);
    }
    if (hash != last_hash) { event("pool scan rows=%u", rows); last_hash = hash; }
    last_error.clear();
    return true;
}

// Resolve the APK sample's data-system entry without patching or instrumenting
// any managed/native code. This is intentionally a metadata-only probe.
static void metadata_probe(FILE *out) {
    void *damage = rpc_find_class("Velvhem.Majo.Gameplay.Ecs.DamageSystem");
    void *team = rpc_find_class("Velvhem.Majo.Gameplay.Ecs.TeamService");
    void *health = rpc_find_class("Velvhem.Majo.Gameplay.Ecs.Health");
    fprintf(out, "healthdiag metadata hook=0 damageClass=%p teamClass=%p healthClass=%p\n",
            damage, team, health);
    if (!damage || !team || !health || !il2cpp_class_get_methods || !il2cpp_method_get_name) return;
    void *iter = nullptr;
    unsigned found = 0;
    while (auto method = il2cpp_class_get_methods((Il2CppClass *)damage, &iter)) {
        const char *name = il2cpp_method_get_name(method);
        if (name && (strstr(name, ".Run") || !strcmp(name, "Run") ||
                     !strcmp(name, "MoveNext"))) {
            fprintf(out, "damage method name=%s VA=%p\n", name, method->methodPointer);
            ++found;
        }
    }
    const char *fields[] = {"_damageRequests", "_healthPool", "_world", "_friendPool", "_enemyPool"};
    const char *owners[] = {"Velvhem.Majo.Gameplay.Ecs.DamageSystem", "Velvhem.Majo.Gameplay.Ecs.DamageSystem", "Velvhem.Majo.Gameplay.Ecs.DamageSystem", "Velvhem.Majo.Gameplay.Ecs.TeamService", "Velvhem.Majo.Gameplay.Ecs.TeamService"};
    for (int i = 0; i < 5; ++i) {
        void *klass = rpc_find_class(owners[i]); size_t off = 0;
        if (klass && rpc_field_offset(klass, fields[i], &off))
            fprintf(out, "damage field owner=%s name=%s offset=0x%zx\n", owners[i], fields[i], off);
    }
    fprintf(out, "healthdiag metadata methods=%u code_patch=0\n", found);
}
static void on_tick(void *, DobbyRegisterContext *context) {
#if defined(__aarch64__)
    // Dobby preserves the original arguments/registers and executes relocated code.
    // No custom prototype, restore/call/repatch cycle, or retained managed pointer.
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (!lock.owns_lock() || !enabled) return;
    const uint64_t now = now_ms();
    if (now >= deadline) { enabled = false; event("healthhook stopped: PC heartbeat expired"); return; }
    ++callbacks;
    if (mode == Mode::Count) return;
    if (now < next_scan) return;
    next_scan = now + interval_ms;
    ++scans;
    scan(context->general.regs.x0);
#endif
}
static bool install(FILE *out) {
#if !defined(__aarch64__)
    fprintf(out, "ERR: healthhook requires arm64\n"); return false;
#else
    if (installed) return true;
    if (!g_rpc_api_ok || !il2cpp_class_get_methods || !il2cpp_method_get_name ||
        !il2cpp_method_get_param_count || !il2cpp_method_get_param ||
        !il2cpp_method_get_return_type || !il2cpp_method_get_flags || !il2cpp_type_get_type) {
        fprintf(out, "ERR: healthhook method metadata APIs unavailable\n"); return false;
    }
    startup_class = rpc_find_class("Velvhem.Majo.Gameplay.Ecs.GameplayEcsStartup");
    character_class = rpc_find_class("Velvhem.Majo.Gameplay.Ecs.BindCharacterSystem");
    health_system_class = rpc_find_class("Velvhem.Majo.Gameplay.Ecs.BindCharacterInfoViewSystem");
    void *hc = rpc_find_class("Velvhem.Majo.Gameplay.Ecs.Health");
    size_t v = 0, m = 0, d = 0, view = 0, hp_offset = 0, ep_offset = 0;
    if (!startup_class || !character_class || !health_system_class || !hc ||
        !rpc_field_offset(hc, "Value", &v) || v != 0x10 ||
        !rpc_field_offset(hc, "MaxValue", &m) || m != 0x18 ||
        !rpc_field_offset(hc, "DamageSourceEntities", &d) || d != 0x20 ||
        !rpc_field_offset(startup_class, "_viewSystems", &view) || view != 0x38 ||
        !rpc_field_offset(health_system_class, "_healthPool", &hp_offset) || hp_offset != 0x50 ||
        !rpc_field_offset(character_class, "_enemyPool", &ep_offset) || ep_offset != 0x120) {
        fprintf(out, "ERR: healthhook class/field layout mismatch\n"); return false;
    }
    const MethodInfo *selected = nullptr;
    unsigned matches = 0;
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods((Il2CppClass *)startup_class, &iter)) {
        const char *name = il2cpp_method_get_name(method);
        if (name && !strcmp(name, "TickView")) { selected = method; ++matches; }
    }
    if (matches != 1 || !selected->methodPointer || il2cpp_method_get_param_count(selected) != 3 ||
        (il2cpp_method_get_flags(selected, nullptr) & METHOD_ATTRIBUTE_STATIC) ||
        il2cpp_type_get_type(il2cpp_method_get_return_type(selected)) != IL2CPP_TYPE_VOID ||
        il2cpp_type_get_type(il2cpp_method_get_param(selected, 0)) != IL2CPP_TYPE_BOOLEAN ||
        il2cpp_type_get_type(il2cpp_method_get_param(selected, 1)) != IL2CPP_TYPE_R4 ||
        il2cpp_type_get_type(il2cpp_method_get_param(selected, 2)) != IL2CPP_TYPE_R4) {
        fprintf(out, "ERR: expected instance void TickView(Boolean,Single,Single)\n"); return false;
    }
    void *address = (void *)selected->methodPointer;
    for (const auto &hook : g_hooks) {
        if (hook.active && (void *)hook.target == address) {
            fprintf(out, "ERR: remove existing generic TickView hook first\n"); return false;
        }
    }
    const int result = DobbyInstrument(address, on_tick);
    if (result != 0) { fprintf(out, "ERR: DobbyInstrument result=%d\n", result); return false; }
    installed = true;
    hook_address = (uint64_t)address;
    fprintf(out, "healthhook installed method=GameplayEcsStartup.TickView VA=%p engine=Dobby\n", address);
    return true;
#endif
}
static void stop() {
    std::lock_guard<std::mutex> lock(mutex);
    enabled = false;
    LOGI("healthhook disconnect stop mode=%s", mode_name());
    // Keep the trampoline installed until process exit, avoiding unhook races.
}
static void command(FILE *out, const char *args) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!strcmp(args, "stop")) {
        enabled = false;
        LOGI("healthhook stop mode=%s", mode_name());
        fprintf(out, "healthhook stopped writes=%" PRIu64 "\n", writes); return;
    }
    if (!strcmp(args, "metadata")) { metadata_probe(out); return; }
    if (!strcmp(args, "status")) {
        if (enabled) deadline = now_ms() + 5000;
        fprintf(out, "healthhook enabled=%d installed=%d callbacks=%" PRIu64 " scans=%" PRIu64
                     " writes=%" PRIu64 " dropped=%" PRIu64 " mode=%s\n", enabled, installed, callbacks, scans, writes, dropped, mode_name());
        for (const auto &line : events) fprintf(out, "%s\n", line.c_str());
        events.clear(); return;
    }
    int64_t value = 1; unsigned milliseconds = 250; char extra;
    Mode requested;
    if (sscanf(args, "count %u %c", &milliseconds, &extra) == 1) requested = Mode::Count;
    else if (sscanf(args, "read %u %c", &milliseconds, &extra) == 1) requested = Mode::Read;
    else if (sscanf(args, "start %" SCNd64 " %u %c", &value, &milliseconds, &extra) == 2)
        requested = Mode::Write;
    else {
        fprintf(out, "ERR: healthhook count <ms> | read <ms> | start <value> <ms> | status | stop\n"); return;
    }
    if (value < 0 || milliseconds < 50 || milliseconds > 10000) {
        fprintf(out, "ERR: invalid healthhook value/interval\n"); return;
    }
    if (!install(out)) return;
    mode = requested;
    target = value; interval_ms = milliseconds; next_scan = 0;
    deadline = now_ms() + 5000; enabled = true;
    callbacks = scans = writes = dropped = 0;
    last_startup = 0; last_hash = UINT64_MAX; events.clear(); last_error.clear();
    fprintf(out, "healthhook started value=%" PRId64 " interval_ms=%u snapshot=false mode=%s\n", target, milliseconds, mode_name());
    LOGI("healthhook start mode=%s pid=%d interval_ms=%u", mode_name(), getpid(), milliseconds);
}
} // namespace health_hook
