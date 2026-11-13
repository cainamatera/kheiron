#include "e2e_common.h"

test_case_t g_test_registry[MAX_TESTS];
size_t g_test_count = 0;

extern void register_tier1_tests(void);
extern void register_tier2_tests(void);
extern void register_tier3_tests(void);
extern void register_tier4_tests(void);

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static bool should_run_in_quick_mode(const test_case_t *tc) {

    if (strncmp(tc->tier_id, "Tier 1", 6) == 0 || strncmp(tc->tier_id, "Tier 2", 6) == 0) {
        return (strstr(tc->name, "_01") != NULL);
    }

    if (strncmp(tc->tier_id, "Tier 3", 6) == 0) {
        return (strcmp(tc->test_id, "T3.02") == 0 ||
                strcmp(tc->test_id, "T3.04") == 0 ||
                strcmp(tc->test_id, "T3.05") == 0 ||
                strcmp(tc->test_id, "T3.06") == 0 ||
                strcmp(tc->test_id, "T3.08") == 0 ||
                strcmp(tc->test_id, "T3.10") == 0);
    }

    if (strncmp(tc->tier_id, "Tier 4", 6) == 0) {
        return (strcmp(tc->test_id, "T4.02") == 0 ||
                strcmp(tc->test_id, "T4.05") == 0);
    }
    return true;
}

int main(int argc, char *argv[]) {
    int target_tier = 0;
    const char *filter = NULL;
    bool no_color = !isatty(fileno(stdout));
    bool quick_mode = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("  --tier <1|2|3|4>   Run tests only for the specified tier\n");
            printf("  --filter <pattern> Run tests matching substring\n");
            printf("  --quick            Run fast smoke test subset (<200ms across all domains)\n");
            printf("  --no-color         Disable ANSI color codes\n");
            return 0;
        } else if (strcmp(argv[i], "--tier") == 0 && i + 1 < argc) {
            target_tier = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else if (strcmp(argv[i], "--quick") == 0) {
            quick_mode = true;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            no_color = true;
        }
    }

    register_tier1_tests();
    register_tier2_tests();
    register_tier3_tests();
    register_tier4_tests();

    const char *c_green = no_color ? "" : "\033[32m";
    const char *c_red   = no_color ? "" : "\033[31m";
    const char *c_yellow = no_color ? "" : "\033[33m";
    const char *c_cyan  = no_color ? "" : "\033[36m";
    const char *c_bold  = no_color ? "" : "\033[1m";
    const char *c_reset = no_color ? "" : "\033[0m";

    printf("%s================================================================================%s\n", c_cyan, c_reset);
    printf("%sKHEIRON-HYDROGEN OPAQUE-BOX E2E TEST SUITE (C99)%s\n", c_bold, c_reset);
    printf("Registered Tests: %zu | Architecture: Linux x86_64%s\n",
           g_test_count, quick_mode ? " | Mode: QUICK SMOKE (<200ms)" : "");
    printf("%s================================================================================%s\n", c_cyan, c_reset);

    size_t passed = 0, failed = 0, skipped = 0;
    const char *last_tier = "";
    double total_start = get_time_ms();

    for (size_t i = 0; i < g_test_count; i++) {
        test_case_t *tc = &g_test_registry[i];

        if (target_tier > 0) {
            int t_num = tc->tier_id[5] - '0';
            if (t_num != target_tier) continue;
        }
        if (filter && strstr(tc->name, filter) == NULL && strstr(tc->test_id, filter) == NULL) {
            continue;
        }
        if (quick_mode && !should_run_in_quick_mode(tc)) {
            continue;
        }

        if (strcmp(tc->tier_id, last_tier) != 0) {
            printf("\n%s[%s]%s\n", c_bold, tc->tier_id, c_reset);
            last_tier = tc->tier_id;
        }

        double t0 = get_time_ms();
        test_status_t res;
        if (tc->is_isolated) {
            res = test_run_isolated(tc->func);
        } else {
            res = tc->func();
        }
        double elapsed = get_time_ms() - t0;

        if (res == TEST_PASS) {
            printf("  [%sPASS%s] %-8s %-48s (%6.2f ms)\n", c_green, c_reset, tc->test_id, tc->name, elapsed);
            passed++;
        } else if (res == TEST_FAIL) {
            printf("  [%sFAIL%s] %-8s %-48s (%6.2f ms)\n", c_red, c_reset, tc->test_id, tc->name, elapsed);
            failed++;
        } else {
            printf("  [%sSKIP%s] %-8s %-48s (%6.2f ms)\n", c_yellow, c_reset, tc->test_id, tc->name, elapsed);
            skipped++;
        }
        fflush(stdout);
    }

    double total_duration = get_time_ms() - total_start;
    printf("\n%s--------------------------------------------------------------------------------%s\n", c_cyan, c_reset);
    printf("SUMMARY:\n");
    printf("  Total Executed : %zu\n", passed + failed + skipped);
    printf("  Passed         : %s%zu%s\n", passed > 0 ? c_green : "", passed, c_reset);
    printf("  Failed         : %s%zu%s\n", failed > 0 ? c_red : "", failed, c_reset);
    printf("  Skipped        : %zu\n", skipped);
    printf("  Total Duration : %.2f ms\n", total_duration);
    printf("--------------------------------------------------------------------------------\n");
    if (failed == 0) {
        printf("%sRESULT: ALL TESTS PASSED (100%% SUCCESS RATE)%s\n", c_green, c_reset);
    } else {
        printf("%sRESULT: SUITE FAILED (%zu FAILURE%s DETECTED)%s\n", c_red, failed, failed > 1 ? "S" : "", c_reset);
    }
    printf("%s================================================================================%s\n", c_cyan, c_reset);

    return failed == 0 ? 0 : 1;
}
