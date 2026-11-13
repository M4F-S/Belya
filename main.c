#include "belya_harness.h"
#include "telegram_adapter.h"
#include "minifrontmatter.h"
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static bool skill_name_safe(const char *n) {
    if (!n) return false;
    size_t len = strlen(n);
    if (len == 0 || len > 64) return false;
    if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)n[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

static void json_escape_append(DynString *out, const char *s) {
    if (!s) return;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"': dyn_str_append(out, "\\\""); break;
            case '\\': dyn_str_append(out, "\\\\"); break;
            case '\n': dyn_str_append(out, "\\n"); break;
            case '\r': dyn_str_append(out, "\\r"); break;
            case '\t': dyn_str_append(out, "\\t"); break;
            default:
                if (*p < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", *p);
                    dyn_str_append(out, esc);
                } else {
                    char ch[2] = { (char)*p, '\0' };
                    dyn_str_append(out, ch);
                }
        }
    }
}

static int belya_export_skills_catalogue(void) {
    const char *skills_dir = "skills";
    DIR *d = opendir(skills_dir);
    if (!d) {
        printf("Error: Could not open '%s' directory for skills export.\n", skills_dir);
        return 1;
    }

    mkdir(".well-known", 0755);
    mkdir(".well-known/agent-skills", 0755);

    DynString json = dyn_str_new();
    dyn_str_append(&json, "{\n  \"version\": \"1.0\",\n  \"protocol\": \"RFC-8615\",\n  \"skills\": [\n");

    struct dirent *entry;
    int count = 0;
    int write_failures = 0;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char skill_md[1024];
        struct stat st;
        snprintf(skill_md, sizeof(skill_md), "%s/%.200s/SKILL.md", skills_dir, entry->d_name);
        if (stat(skill_md, &st) != 0) {
            snprintf(skill_md, sizeof(skill_md), "%s/%.200s", skills_dir, entry->d_name);
            if (stat(skill_md, &st) != 0 || !strstr(entry->d_name, ".md")) continue;
        }

        FILE *f = fopen(skill_md, "r");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 500000) { fclose(f); continue; }

        char *buf = malloc(sz + 1);
        if (!buf) { fclose(f); continue; }
        size_t nr = fread(buf, 1, sz, f);
        buf[nr] = '\0';
        fclose(f);

        Frontmatter *fm = frontmatter_parse(buf);
        const char *fm_name = fm ? frontmatter_get_scalar(fm, "name") : NULL;
        const char *fm_desc = fm ? frontmatter_get_scalar(fm, "description") : NULL;
        const char *name = fm_name ? fm_name : entry->d_name;
        const char *desc = fm_desc ? fm_desc : name;
        /* H28: name is a path component below; reject anything that could
           escape .well-known (e.g. "../../skills/good") or break the JSON. */
        if (!skill_name_safe(name)) {
            fprintf(stderr, "export-skills: skipping unsafe skill name '%s'\n", name ? name : "(null)");
            if (fm) frontmatter_free(fm);
            free(buf);
            continue;
        }

        char dest_dir[512];
        snprintf(dest_dir, sizeof(dest_dir), ".well-known/agent-skills/%.200s", name);
        mkdir(dest_dir, 0755);
        char dest_file[1024];
        snprintf(dest_file, sizeof(dest_file), "%s/SKILL.md", dest_dir);
        FILE *df = fopen(dest_file, "w");
        if (!df || fwrite(buf, 1, nr, df) != nr || fclose(df) != 0) {
            fprintf(stderr, "export-skills: failed to write '%s'\n", dest_file);
            write_failures++;
            if (fm) frontmatter_free(fm);
            free(buf);
            continue;
        }

        if (count > 0) dyn_str_append(&json, ",\n");
        dyn_str_append(&json, "    {\n      \"name\": \"");
        json_escape_append(&json, name);
        dyn_str_append(&json, "\",\n      \"description\": \"");
        json_escape_append(&json, desc);
        dyn_str_append(&json, "\",\n      \"path\": \"/.well-known/agent-skills/");
        json_escape_append(&json, name);
        dyn_str_append(&json, "/SKILL.md\"\n    }");
        count++;

        if (fm) frontmatter_free(fm);
        free(buf);
    }
    closedir(d);

    dyn_str_append(&json, "\n  ]\n}\n");

    if (count == 0 || write_failures > 0) {
        fprintf(stderr, "export-skills: %d written, %d failed, catalog not published\n",
                count, write_failures);
        dyn_str_free(&json);
        return 1;
    }

    FILE *jf = fopen(".well-known/agent-skills/index.json", "w");
    if (!jf) {
        fprintf(stderr, "export-skills: cannot create index.json\n");
        dyn_str_free(&json);
        return 1;
    }
    size_t want = strlen(json.data);
    if (fwrite(json.data, 1, want, jf) != want || fclose(jf) != 0) {
        fprintf(stderr, "export-skills: failed to write index.json\n");
        dyn_str_free(&json);
        return 1;
    }
    dyn_str_free(&json);

    printf("✓ Successfully exported %d skills to .well-known/agent-skills/index.json (RFC 8615)\n", count);
    return 0;
}

static void cleanup_terminal(void) {
    if (isatty(STDOUT_FILENO)) {
        const char *reset_seq = "\033[?25h\033[0m\n";
        ssize_t wrc = write(STDOUT_FILENO, reset_seq, strlen(reset_seq));
        (void)wrc;
    }
}

static void sigint_handler(int sig) {
    cleanup_terminal();
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
    raise(sig);
}

static void load_env_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\r' || *p == '\n' || *p == '\0') continue;
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *key = p;
            char *val = eq + 1;
            char *end_key = key + strlen(key) - 1;
            while (end_key > key && (*end_key == ' ' || *end_key == '\t')) *end_key-- = '\0';
            while (*val == ' ' || *val == '\t') val++;
            char *end_val = val + strlen(val) - 1;
            while (end_val >= val && (*end_val == '\r' || *end_val == '\n' || *end_val == ' ' || *end_val == '\t')) *end_val-- = '\0';
            if (strlen(key) > 0 && strlen(val) > 0) {
                setenv(key, val, 0); // 0 means do not overwrite already set environment variables
            }
        }
    }
    fclose(fp);
}

int main(int argc, char **argv) {
    atexit(cleanup_terminal);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    load_env_file(".env");

    // Unbuffered stdout for systemd journal visibility
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    bool telegram_mode = false;
    bool agency_mode = false;
    bool company_mode = false;
    const char *resume_session_id = NULL;
    const char *headless_prompt = NULL;
    const char *stats_json_path = NULL;
    bool no_probe = false;
    bool stage_mode = false;
    const char *preset_name = NULL;
    bool probe_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: belya [OPTIONS]\n\n");
            printf("Options:\n");
            printf("  -h, --help                 Show this help message\n");
            printf("  -P, --preset <name>        Load model preset (falcon, falcon-vllm, falcon2, qwen, deepseek, airgap)\n");
            printf("  --presets                  List all available UAE & championship model presets and exit\n");
            printf("  --probe                    Execute gateway pre-flight health & authentication probe and exit\n");
            printf("  -S, --stage, --telemetry   Enable live on-stage interactive visual telemetry HUD\n");
            printf("  -t, --telegram             Run as Telegram (+ Omnichannel) bot daemon\n");
            printf("  -o, --omnichannel          Alias for --telegram omnichannel daemon mode\n");
            printf("  -d, --docker <container>   Execute bash commands inside Docker sandbox container\n");
            printf("  -a, --agency [prompt]      Execute task via Belya Agency multi-agent pipeline\n");
            printf("  -c, --company [task]       Execute enterprise SDLC company simulation or team status\n");
            printf("  -r, --resume <session_id>  Resume saved conversation session\n");
            printf("  -p, --prompt <prompt>      Execute headless mission prompt and exit\n");
            printf("  --no-probe                 Skip gateway pre-flight health & authentication probe\n");
            printf("  --export-skills            Export RFC 8615 skills discovery catalogue to .well-known/agent-skills/ and exit\n");
            printf("  --export-sharegpt <file>   Export trajectory in ShareGPT / Hermes format and exit\n");
            printf("  --export-dpo <file>        Export trajectory in Hermes DPO preference format and exit\n");
            printf("  --headless <prompt>        Alias for --prompt\n");
            printf("  --eval <prompt>            Alias for --prompt\n\n");
            return 0;
        } else if (strcmp(argv[i], "--presets") == 0) {
            model_presets_print_list();
            return 0;
        } else if (strcmp(argv[i], "--export-skills") == 0) {
            return belya_export_skills_catalogue();
        } else if (strcmp(argv[i], "--export-sharegpt") == 0 && i + 1 < argc) {
            const char *outf = argv[++i];
            BelyaAgent *ag = belya_agent_init(NULL, "belya_memory.sqlite", NULL);
            bool ok = belya_agent_export_sharegpt(ag, NULL, outf);
            belya_agent_free(ag);
            return ok ? 0 : 1;
        } else if (strcmp(argv[i], "--export-dpo") == 0 && i + 1 < argc) {
            const char *outf = argv[++i];
            BelyaAgent *ag = belya_agent_init(NULL, "belya_memory.sqlite", NULL);
            bool ok = belya_agent_export_dpo(ag, NULL, outf);
            belya_agent_free(ag);
            return ok ? 0 : 1;
        } else if ((strcmp(argv[i], "--preset") == 0 || strcmp(argv[i], "-P") == 0) && i + 1 < argc) {
            preset_name = argv[++i];
        } else if (strcmp(argv[i], "--probe") == 0) {
            probe_only = true;
        } else if (strcmp(argv[i], "--stage") == 0 || strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--telemetry") == 0) {
            stage_mode = true;
            setenv("BELYA_STAGE_TELEMETRY", "1", 1);
        } else if (strcmp(argv[i], "--telegram") == 0 || strcmp(argv[i], "-t") == 0 ||
                   strcmp(argv[i], "--omnichannel") == 0 || strcmp(argv[i], "-o") == 0) {
            telegram_mode = true;
        } else if (strcmp(argv[i], "--no-probe") == 0) {
            no_probe = true;
        } else if ((strcmp(argv[i], "--docker") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
            setenv("EXEC_BACKEND", "docker", 1);
            setenv("DOCKER_CONTAINER", argv[++i], 1);
        } else if (strcmp(argv[i], "--agency") == 0 || strcmp(argv[i], "-a") == 0) {
            agency_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                headless_prompt = argv[++i];
            }
        } else if (strcmp(argv[i], "--company") == 0 || strcmp(argv[i], "-c") == 0) {
            company_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                headless_prompt = argv[++i];
            }
        } else if ((strcmp(argv[i], "--resume") == 0 || strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--session") == 0) && i + 1 < argc) {
            resume_session_id = argv[++i];
        } else if ((strcmp(argv[i], "--headless") == 0 || strcmp(argv[i], "--eval") == 0 || strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--prompt") == 0) && i + 1 < argc) {
            headless_prompt = argv[++i];
        } else if (strcmp(argv[i], "--stats-json") == 0 && i + 1 < argc) {
            stats_json_path = argv[++i];
        } else if (argv[i][0] != '-' && !headless_prompt) {
            headless_prompt = argv[i];
        }
    }

    if (preset_name) {
        const ModelPresetConfig *preset = model_preset_get(preset_name);
        if (preset) {
            setenv("MODEL_NAME", preset->model_name, 1);
            setenv("MODEL_ENDPOINT", preset->endpoint, 1);
            if (preset->is_airgap) {
                setenv("AIRGAP_MODE", "1", 1);
            }
            printf("\033[1;32m[Preset Applied]:\033[0m %s -> Model: %s (%s)\n",
                   preset->name, preset->model_name, preset->description);
        } else {
            fprintf(stderr, "\033[1;31m[Error] Unknown preset '%s'. Run 'belya --presets' for options.\033[0m\n", preset_name);
            return 1;
        }
    }

    const char *endpoint = getenv("MODEL_ENDPOINT");
    const char *api_key = getenv("MODEL_API_KEY");
    const char *model = getenv("MODEL_NAME");
    const char *timeout_env = getenv("MODEL_TIMEOUT");
    const char *retries_env = getenv("MODEL_MAX_RETRIES");

    const char *tg_token = getenv("TELEGRAM_BOT_TOKEN");
    const char *tg_chat_id = getenv("TELEGRAM_CHAT_ID");

    if (!endpoint) endpoint = "http://localhost:11434/v1/chat/completions";
    if (!api_key)  api_key = "none";
    if (!model)    model = "hermes-3";

    // 1. Initialize Model Gateway
    ModelGateway *gateway = model_gateway_init(endpoint, api_key, model);
    if (!gateway) {
        fprintf(stderr, "\033[1;31m[Error] Failed to initialize model gateway.\033[0m\n");
        return 1;
    }
    if (timeout_env && atoi(timeout_env) > 0) {
        gateway->timeout_sec = atoi(timeout_env);
    }
    if (retries_env && atoi(retries_env) >= 0) {
        gateway->max_retries = atoi(retries_env);
    }

    if (telegram_mode) {
        gateway->streaming = true;
    }

    // Pre-flight health and authentication probe
    const char *probe_env = getenv("GATEWAY_PREFLIGHT_PROBE");
    if (probe_only || (!no_probe && (!probe_env || (strcmp(probe_env, "0") != 0 && strcmp(probe_env, "false") != 0)))) {
        ModelGatewayProbeResult probe = model_gateway_preflight_probe(gateway);
        model_gateway_print_probe_report(&probe);
        bool all_ok = probe.all_ok;
        model_gateway_probe_result_free(&probe);
        if (probe_only) {
            model_gateway_free(gateway);
            return all_ok ? 0 : 1;
        }
    }

    // 2. Initialize Belya Agent with persistent SQLite memory & Strategic Execution Directives

    const char *default_system_prompt =
        "Role & Objective:\n"
        "Act as Belya, an expert autonomous AI software engineer and security execution harness implemented in pure C99. You run natively on the host system (macOS / Linux) with full POSIX, bash, and filesystem access. Your goal is to complete the task with absolute accuracy, zero assumptions, and strict verification.\n\n"
        "Core Rules:\n"
        "1. Host Access & Native Execution Mandate: You run natively on the host system with direct POSIX, bash, filesystem, and shell execution privileges. NEVER claim you lack access to the computer, terminal, files, GUI, or operating system. If a task requires terminal manipulation, system configuration, file operations, or running commands, invoke your `bash` or native tools immediately.\n"
        "2. Verify Everything: Never assume facts, syntax, or outcomes. Treat every data point as unverified until proven otherwise.\n"
        "3. Research Deeply: Conduct thorough research using primary sources, official documentation, and local source trees.\n"
        "4. Test Continuously: Run tests at every critical stage. Verify that code, logic, or data works in practice, not just in theory.\n"
        "5. Don't reinvent the wheel; instead, leverage proven frameworks and best practices from past successes.\n"
        "6. Zero-Tolerance Memory Safety: Always check allocation returns (malloc/calloc != NULL), validate pointer bounds, free every resource deterministically, and guarantee zero memory leaks or undefined behavior.\n"
        "7. Tool Fallback: If a tool fails or reports a missing key/provider, try at least one alternative (a different provider, a direct HTTP/API call, or local data) before telling the user you cannot do something. A missing credential is not a capability limit until a fallback has been tried.\n"
        "8. No Negative Claims From Failed Probes: If a command fails, is blocked, or never ran, do not conclude that a capability is missing. Re-run it correctly or mark the point unverified. Absence of evidence is not evidence of absence.\n\n"
        "Execution Protocol:\n"
        "1. OBSERVE: Before modifying any file, ALWAYS call read_file first to verify its exact current contents. Do not guess line numbers, indentation, or whitespace.\n"
        "2. THINK: State your hypothesis and reasoning in your response text BEFORE calling any tool. Explain WHY you chose this action.\n"
        "3. ACT: Execute one focused tool call at a time. Check the result before proceeding.\n"
        "4. VERIFY: After editing code, run the compiler or test suite to confirm your change works. If it fails, re-read the file and try a different approach.\n"
        "5. If you fail an edit 3 times, STOP retrying the same approach. Re-read the file with read_file, identify what changed, and formulate a completely new strategy.\n"
        "6. Git Workflow: Work strictly within a Git repository. Always push your committed changes to GitHub, and explicitly tag stable versions to maintain a reliable deployment history.\n"
        "7. Autonomous Multi-Step Execution: When given a multi-step mission, execute all steps continuously using tool calls without stopping or generating conversational chit-chat between intermediate steps. Only output your final summary once all stages are 100% complete.\n"
        "8. Conversational Fast-Path: For greetings (e.g., 'good morning', 'hello'), pleasantries, questions about your status/capabilities, or direct queries that do not require tool actions, respond directly, politely, and concisely in a single turn with zero tool calls.\n\n"
        "Tool Constraints:\n"
        "- edit_file: old_text must EXACTLY match the file content character-for-character, including all leading spaces, tabs, and newlines. ALWAYS read_file first.\n"
        "- bash: Do NOT run interactive commands (vim, nano, top, less, man, sudo). They will hang. Use cat, head, tail, sed, awk, grep instead.\n"
        "- search_files: Returns max 50 matches. Use file_glob or regex to narrow scope.\n"
        "- For large files (>200 lines), use read_file with offset and limit parameters.";

    /* Telegram is a phone chat, not a terminal. The CLI prompt above asks for
       hypothesis narration and engineering evidence; on Telegram that reaches
       the user as a wall of steps and code. This contract overrides it for the
       daemon only. */
    const char *telegram_channel_contract =
        "\n\n=== Channel Contract (Telegram) ===\n"
        "You are replying to a person in a phone chat. They read your message, not your terminal or tool log.\n"
        "- Answer the actual question first, in plain conversational prose. Lead with the answer.\n"
        "- Default to about 150 words (1-3 short paragraphs). If the answer needs more, give the short version and offer to expand; go long only when the user explicitly asks for detail, a plan or a report.\n"
        "- Do NOT include code blocks, file paths, line numbers, command lines, tool names, JSON or telemetry (turn/step/token counts) unless the user explicitly asks for code.\n"
        "- Do NOT narrate your hypothesis, investigation or tool calls. Rule 2 of the Execution Protocol (state the hypothesis in response text) does not apply on this channel; report the outcome only.\n"
        "- If work is unfinished, say so in one sentence and name the next action. Do not attach a step list.\n"
        "- No headings, no tables, no 'Bottom line'/'Open items' report scaffolding.\n"
        "These formatting rules take precedence over any repository guideline that asks for reports or evidence dumps.\n";

    char *daemon_prompt = NULL;
    if (telegram_mode) {
        size_t plen = strlen(default_system_prompt) + strlen(telegram_channel_contract) + 1;
        daemon_prompt = malloc(plen);
        if (daemon_prompt) {
            snprintf(daemon_prompt, plen, "%s%s", default_system_prompt, telegram_channel_contract);
        }
    }

    BelyaAgent *agent = belya_agent_init(gateway, "belya_memory.sqlite",
                                         daemon_prompt ? daemon_prompt : default_system_prompt);
    free(daemon_prompt);
    if (!agent) {
        fprintf(stderr, "\033[1;31m[Error] Failed to initialize agent (OOM or DB failure).\033[0m\n");
        model_gateway_free(gateway);
        return 1;
    }

    // If resume flag is provided, restore session.
    // In telegram mode with no explicit resume flag, auto-resume active telegram session if present.
    if (resume_session_id) {
        if (belya_agent_load_session(agent, resume_session_id)) {
            printf("\033[1;32m[Session Restored]\033[0m Successfully resumed session '%s' (%zu messages loaded).\n", resume_session_id, agent->msg_count);
        } else {
            printf("\033[1;33m[Session Alert]\033[0m Session '%s' not found. Starting fresh session.\n", resume_session_id);
        }
    } else if (telegram_mode) {
        if (belya_agent_load_session(agent, "telegram_active")) {
            printf("\033[1;32m[Telegram Session Restored]\033[0m Successfully auto-resumed 'telegram_active' (%zu messages loaded).\n", agent->msg_count);
        }
    }

    // 3. Initialize Belya Harness & register execution engine
    BelyaHarness *harness = belya_harness_init(agent);
    if (!harness) {
        fprintf(stderr, "\033[1;31m[Error] Out of memory: failed to initialize harness.\033[0m\n");
        return 1;
    }
    if (stage_mode) {
        harness->stage_mode = true;
    }

    if (telegram_mode) {
        if (!tg_token || strlen(tg_token) == 0) {
            fprintf(stderr, "\033[1;31m[Error] Telegram mode requires TELEGRAM_BOT_TOKEN environment variable.\033[0m\n");
            fprintf(stderr, "Example:\n  export TELEGRAM_BOT_TOKEN=\"123456789:ABCDefGhIJKlmNoPQRsTUVwxyZ\"\n  export TELEGRAM_CHAT_ID=\"987654321\"\n  ./belya --telegram\n\n");
            belya_harness_free(harness);
            model_gateway_free(gateway);
            return 1;
        }

        TelegramBot *bot = telegram_bot_init(tg_token, tg_chat_id);
        if (!bot) {
            fprintf(stderr, "\033[1;31m[Error] Out of memory: failed to initialize Telegram bot.\033[0m\n");
            belya_harness_free(harness);
            model_gateway_free(gateway);
            return 1;
        }
        telegram_bot_run(bot, harness);
        telegram_bot_free(bot);
    } else if (company_mode) {
        printf("Executing Belya Sovereign Software Company Simulation (Model: %s)...\n\n", model);
        char *resp = belya_company_handle_command(harness, headless_prompt ? headless_prompt : "Autonomous Enterprise Software Development");
        if (resp) {
            printf("\n%s\n", resp);
            free(resp);
        }
    } else if (headless_prompt) {
        if (agency_mode) {
            printf("Executing Belya Agency Multi-Agent Pipeline (Model: %s):\n\"%s\"\n\n", model, headless_prompt);
            char **pipeline = NULL;
            size_t count = 0;
            char *direct = NULL;
            belya_agency_triage(harness, headless_prompt, &pipeline, &count, &direct);
            if (direct) {
                printf("[Belya Triage Direct Response]:\n%s\n\n", direct);
                free(direct);
            } else if (count > 0) {
                char *report = belya_agency_execute_pipeline(harness, headless_prompt, (const char **)pipeline, count);
                if (report) {
                    printf("\n%s\n", report);
                    free(report);
                }
                for (size_t k = 0; k < count; k++) free(pipeline[k]);
                free(pipeline);
            }
        } else {
            printf("Executing headless mission (Model: %s):\n\"%s\"\n\n", model, headless_prompt);
            belya_harness_execute_turn(harness, headless_prompt);
            if (stats_json_path) {
                JsonValue *st = json_create_object();
                if (st) {
                    json_obj_add(st, "ok", json_create_bool(true));
                    json_obj_add(st, "model", json_create_string(model ? model : ""));
                    json_obj_add(st, "prompt_tokens", json_create_number((double)agent->total_prompt_tokens));
                    json_obj_add(st, "completion_tokens", json_create_number((double)agent->total_completion_tokens));
                    json_obj_add(st, "cached_tokens", json_create_number((double)agent->total_cached_tokens));
                    json_obj_add(st, "steps", json_create_number((double)harness->last_turn_steps));
                    json_obj_add(st, "tool_calls", json_create_number((double)harness->last_turn_tool_calls));
                    json_obj_add(st, "elapsed_ms", json_create_number(harness->last_turn_latency_ms));
                    char *js = json_serialize(st);
                    if (js) {
                        FILE *sf = fopen(stats_json_path, "w");
                        if (sf) {
                            fputs(js, sf);
                            fputc('\n', sf);
                            fclose(sf);
                        }
                        free(js);
                    }
                    json_free(st);
                }
            }
            if (agent->total_prompt_tokens > 0 || agent->total_completion_tokens > 0) {
                printf("\n[Session Economics]: Prompt Tokens: %zu | Completion Tokens: %zu | Cached Tokens: %zu | Total: %zu\n",
                    agent->total_prompt_tokens, agent->total_completion_tokens, agent->total_cached_tokens,
                    agent->total_prompt_tokens + agent->total_completion_tokens);
            }
        }
    } else {
        printf("Starting Belya Harness with endpoint: %s (Model: %s)\n", endpoint, model);
        belya_harness_repl(harness);
    }

    // 4. Cleanup
    belya_harness_free(harness);
    model_gateway_free(gateway);

    return 0;
}
