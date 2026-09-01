#include "c_agent.h"
#include <time.h>

static void free_single_message(AgentMessage *m) {
    if (!m) return;
    if (m->role) { free(m->role); m->role = NULL; }
    if (m->content) { free(m->content); m->content = NULL; }
    if (m->tool_call_id) { free(m->tool_call_id); m->tool_call_id = NULL; }
    if (m->tool_calls) {
        for (size_t k = 0; k < m->tool_call_count; k++) {
            if (m->tool_calls[k].id) free(m->tool_calls[k].id);
            if (m->tool_calls[k].name) free(m->tool_calls[k].name);
            if (m->tool_calls[k].arguments_json) free(m->tool_calls[k].arguments_json);
        }
        free(m->tool_calls);
        m->tool_calls = NULL;
    }
    m->tool_call_count = 0;
}

CAgent *c_agent_init(ModelGateway *gw, const char *db_path, const char *system_instructions) {
    CAgent *agent = calloc(1, sizeof(CAgent));
    agent->gateway = gw;
    agent->msg_cap = 64;
    agent->messages = calloc(agent->msg_cap, sizeof(AgentMessage));
    agent->max_context_messages = 50;
    
    const char *tok_budget_env = getenv("MAX_CONTEXT_TOKENS");
    agent->max_context_tokens = (tok_budget_env && atoi(tok_budget_env) > 0) ? (size_t)atoi(tok_budget_env) : 128000;

    agent->auto_save_interval = 5;
    agent->turn_count = 0;
    agent->turns_since_save = 0;

    // Initialize SQLite memory and session store
    if (sqlite3_open(db_path, &agent->db) == SQLITE_OK) {
        // Enable WAL mode for better concurrent read performance
        sqlite3_exec(agent->db, "PRAGMA journal_mode=WAL;", 0, 0, 0);

        const char *schema_sql = 
            "CREATE TABLE IF NOT EXISTS agent_memory ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  topic TEXT NOT NULL,"
            "  content TEXT NOT NULL,"
            "  wing TEXT DEFAULT 'default',"
            "  room TEXT DEFAULT 'general',"
            "  salience REAL DEFAULT 1.0,"
            "  access_count INTEGER DEFAULT 0,"
            "  last_accessed_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS agent_timeline ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  event_type TEXT NOT NULL,"
            "  summary TEXT NOT NULL,"
            "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS sessions ("
            "  id TEXT PRIMARY KEY,"
            "  title TEXT,"
            "  model TEXT,"
            "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS session_messages ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  session_id TEXT NOT NULL,"
            "  idx INTEGER NOT NULL,"
            "  role TEXT NOT NULL,"
            "  content TEXT NOT NULL,"
            "  tool_call_id TEXT,"
            "  tool_calls_json TEXT,"
            "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";
        sqlite3_exec(agent->db, schema_sql, 0, 0, 0);

        // Safe column migration for existing databases
        const char *migrations[] = {
            "ALTER TABLE agent_memory ADD COLUMN wing TEXT DEFAULT 'default';",
            "ALTER TABLE agent_memory ADD COLUMN room TEXT DEFAULT 'general';",
            "ALTER TABLE agent_memory ADD COLUMN salience REAL DEFAULT 1.0;",
            "ALTER TABLE agent_memory ADD COLUMN access_count INTEGER DEFAULT 0;",
            "ALTER TABLE agent_memory ADD COLUMN last_accessed_at DATETIME DEFAULT CURRENT_TIMESTAMP;",
            NULL
        };
        for (int mi = 0; migrations[mi]; mi++) {
            sqlite3_exec(agent->db, migrations[mi], 0, 0, 0);
        }

        // Check if FTS5 is available
        const char *fts_sql = "CREATE VIRTUAL TABLE IF NOT EXISTS agent_memory_fts USING fts5(topic, content, wing, room);";
        if (sqlite3_exec(agent->db, fts_sql, 0, 0, 0) == SQLITE_OK) {
            agent->has_fts5 = true;
        } else {
            // Fallback to basic 2-column FTS5
            const char *fts_sql2 = "CREATE VIRTUAL TABLE IF NOT EXISTS agent_memory_fts USING fts5(topic, content);";
            if (sqlite3_exec(agent->db, fts_sql2, 0, 0, 0) == SQLITE_OK) {
                agent->has_fts5 = true;
            }
        }
    }

    // Set Hermes System Core
    DynString sys = dyn_str_new();
    dyn_str_append(&sys, system_instructions);
    dyn_str_append(&sys, "\nYou are C Agent, an autonomous software engine. Use your tools sequentially to solve tasks.");

    // Check for repository guidelines (.agentrules / AGENT.md / CLAUDE.md)
    const char *rules_files[] = {".agentrules", "AGENT.md", "CLAUDE.md", NULL};
    for (int rf = 0; rules_files[rf]; rf++) {
        FILE *rfp = fopen(rules_files[rf], "r");
        if (rfp) {
            fseek(rfp, 0, SEEK_END);
            long rsz = ftell(rfp);
            fseek(rfp, 0, SEEK_SET);
            if (rsz > 0 && rsz < 50000) {
                char *rbuf = malloc(rsz + 1);
                if (rbuf) {
                    size_t rread = fread(rbuf, 1, rsz, rfp);
                    rbuf[rread] = '\0';
                    dyn_str_append(&sys, "\n\n=== Repository Guidelines (");
                    dyn_str_append(&sys, rules_files[rf]);
                    dyn_str_append(&sys, ") ===\n");
                    dyn_str_append(&sys, rbuf);
                    free(rbuf);
                }
            }
            fclose(rfp);
            break;
        }
    }

    c_agent_add_message(agent, "system", sys.data);
    dyn_str_free(&sys);

    return agent;
}

void c_agent_register_schema(CAgent *agent, const char *name, const char *desc, JsonValue *params) {
    agent->schema_count++;
    AgentToolSchema *new_schemas = realloc(agent->schemas, sizeof(AgentToolSchema) * agent->schema_count);
    if (!new_schemas) {
        fprintf(stderr, "[Fatal] Out of memory in c_agent_register_schema\n");
        abort();
    }
    agent->schemas = new_schemas;
    AgentToolSchema *s = &agent->schemas[agent->schema_count - 1];
    s->name = strdup(name);
    s->description = strdup(desc);
    s->parameters_schema = params;
}

void c_agent_add_message(CAgent *agent, const char *role, const char *content) {
    if (agent->msg_count >= agent->msg_cap) {
        agent->msg_cap *= 2;
        AgentMessage *new_msgs = realloc(agent->messages, sizeof(AgentMessage) * agent->msg_cap);
        if (!new_msgs) {
            fprintf(stderr, "[Fatal] Out of memory in c_agent_add_message\n");
            abort();
        }
        agent->messages = new_msgs;
    }
    AgentMessage *m = &agent->messages[agent->msg_count++];
    memset(m, 0, sizeof(AgentMessage));
    m->role = strdup(role);
    m->content = strdup(content ? content : "");
}

void c_agent_add_tool_result(CAgent *agent, const char *tool_call_id, const char *name, const char *result) {
    (void)name;
    if (agent->msg_count >= agent->msg_cap) {
        agent->msg_cap *= 2;
        AgentMessage *new_msgs = realloc(agent->messages, sizeof(AgentMessage) * agent->msg_cap);
        if (!new_msgs) {
            fprintf(stderr, "[Fatal] Out of memory in c_agent_add_tool_result\n");
            abort();
        }
        agent->messages = new_msgs;
    }
    AgentMessage *m = &agent->messages[agent->msg_count++];
    memset(m, 0, sizeof(AgentMessage));
    m->role = strdup("tool");
    m->tool_call_id = strdup(tool_call_id ? tool_call_id : "call_default");
    m->content = strdup(result ? result : "");
}

void c_agent_log_timeline(CAgent *agent, const char *event_type, const char *summary) {
    if (!agent || !agent->db || !event_type || !summary) return;
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO agent_timeline (event_type, summary) VALUES (?, ?);";
    if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, event_type, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, summary, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

char *c_agent_get_timeline(CAgent *agent, int limit) {
    if (!agent || !agent->db) return strdup("Timeline database not active.");
    if (limit <= 0) limit = 15;

    sqlite3_stmt *stmt;
    const char *sql = "SELECT event_type, summary, created_at FROM agent_timeline ORDER BY id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return strdup("Failed to query agent timeline.");
    }

    sqlite3_bind_int(stmt, 1, limit);
    DynString ds = dyn_str_new();
    dyn_str_append(&ds, "=== Agent Timeline Log ===\n");
    size_t count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *ev = (const char *)sqlite3_column_text(stmt, 0);
        const char *sum = (const char *)sqlite3_column_text(stmt, 1);
        const char *ts = (const char *)sqlite3_column_text(stmt, 2);
        dyn_str_appendf(&ds, "[%s] \033[1;36m%-16s\033[0m: %s\n", ts ? ts : "", ev ? ev : "", sum ? sum : "");
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) dyn_str_append(&ds, "No timeline events recorded yet.");
    return ds.data;
}

void c_agent_persist_memory_scoped(CAgent *agent, const char *topic, const char *content, const char *wing, const char *room) {
    if (!agent || !agent->db || !topic || !content) return;
    const char *w = (wing && strlen(wing) > 0) ? wing : "default";
    const char *r = (room && strlen(room) > 0) ? room : "general";

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO agent_memory (topic, content, wing, room, salience, access_count) VALUES (?, ?, ?, ?, 1.0, 0);";
    if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, topic, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, content, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, w, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, r, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    if (agent->has_fts5) {
        const char *fts_insert = "INSERT INTO agent_memory_fts (topic, content) VALUES (?, ?);";
        if (sqlite3_prepare_v2(agent->db, fts_insert, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, topic, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, content, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    char log_summary[256];
    snprintf(log_summary, sizeof(log_summary), "[%s/%s] %s", w, r, topic);
    c_agent_log_timeline(agent, "memory_persisted", log_summary);
}

void c_agent_persist_memory(CAgent *agent, const char *topic, const char *content) {
    if (!topic || !content) return;

    char wing[64] = "default";
    char room[64] = "general";
    const char *actual_topic = topic;

    // Check for wing/room prefix: "wing/room: topic" or "wing:room:topic"
    const char *colon = strchr(topic, ':');
    const char *slash = strchr(topic, '/');

    if (slash && colon && slash < colon) {
        size_t wlen = slash - topic;
        size_t rlen = colon - (slash + 1);
        if (wlen < sizeof(wing) && rlen < sizeof(room)) {
            strncpy(wing, topic, wlen); wing[wlen] = '\0';
            strncpy(room, slash + 1, rlen); room[rlen] = '\0';
            actual_topic = colon + 1;
            while (*actual_topic == ' ') actual_topic++;
        }
    }

    c_agent_persist_memory_scoped(agent, actual_topic, content, wing, room);
}

char *c_agent_search_memory(CAgent *agent, const char *query) {
    if (!agent->db) return strdup("Memory database not active.");
    DynString out = dyn_str_new();

    if (agent->has_fts5 && query && strlen(query) > 0) {
        sqlite3_stmt *stmt;
        const char *sql = 
            "SELECT m.id, m.topic, m.content, m.wing, m.room, m.salience, m.access_count "
            "FROM agent_memory m "
            "JOIN agent_memory_fts f ON m.rowid = f.rowid "
            "WHERE agent_memory_fts MATCH ? "
            "ORDER BY (m.salience * 1.5 - rank) DESC LIMIT 5;";
        
        if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int mid = sqlite3_column_int(stmt, 0);
                const char *top = (const char *)sqlite3_column_text(stmt, 1);
                const char *txt = (const char *)sqlite3_column_text(stmt, 2);
                const char *w = (const char *)sqlite3_column_text(stmt, 3);
                const char *r = (const char *)sqlite3_column_text(stmt, 4);
                double sal = sqlite3_column_double(stmt, 5);
                int acc = sqlite3_column_int(stmt, 6);

                dyn_str_appendf(&out, "=== Memory Entry [Wing: %s | Room: %s | Salience: %.1f | Hits: %d] ===\nTopic: %s\nKnowledge: %s\n\n",
                    w ? w : "default", r ? r : "general", sal, acc + 1, top ? top : "", txt ? txt : "");

                // Update recency & salience boost
                sqlite3_stmt *up_stmt;
                const char *up_sql = "UPDATE agent_memory SET access_count = access_count + 1, "
                                     "salience = MIN(salience + 0.1, 5.0), last_accessed_at = CURRENT_TIMESTAMP WHERE id = ?;";
                if (sqlite3_prepare_v2(agent->db, up_sql, -1, &up_stmt, NULL) == SQLITE_OK) {
                    sqlite3_bind_int(up_stmt, 1, mid);
                    sqlite3_step(up_stmt);
                    sqlite3_finalize(up_stmt);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    if (out.len == 0) {
        sqlite3_stmt *stmt;
        const char *sql = "SELECT id, topic, content, wing, room, salience, access_count "
                          "FROM agent_memory WHERE topic LIKE ? OR content LIKE ? OR wing LIKE ? OR room LIKE ? "
                          "ORDER BY salience DESC, id DESC LIMIT 5;";
        if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            char pattern[256];
            snprintf(pattern, sizeof(pattern), "%%%s%%", query ? query : "");
            sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, pattern, -1, SQLITE_STATIC);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int mid = sqlite3_column_int(stmt, 0);
                const char *top = (const char *)sqlite3_column_text(stmt, 1);
                const char *txt = (const char *)sqlite3_column_text(stmt, 2);
                const char *w = (const char *)sqlite3_column_text(stmt, 3);
                const char *r = (const char *)sqlite3_column_text(stmt, 4);
                double sal = sqlite3_column_double(stmt, 5);
                int acc = sqlite3_column_int(stmt, 6);

                dyn_str_appendf(&out, "=== Memory Entry [Wing: %s | Room: %s | Salience: %.1f | Hits: %d] ===\nTopic: %s\nKnowledge: %s\n\n",
                    w ? w : "default", r ? r : "general", sal, acc + 1, top ? top : "", txt ? txt : "");

                // Update recency
                sqlite3_stmt *up_stmt;
                const char *up_sql = "UPDATE agent_memory SET access_count = access_count + 1, "
                                     "salience = MIN(salience + 0.1, 5.0), last_accessed_at = CURRENT_TIMESTAMP WHERE id = ?;";
                if (sqlite3_prepare_v2(agent->db, up_sql, -1, &up_stmt, NULL) == SQLITE_OK) {
                    sqlite3_bind_int(up_stmt, 1, mid);
                    sqlite3_step(up_stmt);
                    sqlite3_finalize(up_stmt);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    if (out.len == 0) dyn_str_append(&out, "No matching memory records located.");
    return out.data;
}

void c_agent_compact_history(CAgent *agent, size_t keep_recent) {
    if (!agent || agent->msg_count <= 1 + keep_recent) return;

    size_t drop_count = agent->msg_count - 1 - keep_recent;
    for (size_t i = 1; i <= drop_count; i++) {
        free_single_message(&agent->messages[i]);
    }

    size_t remaining = keep_recent;
    for (size_t i = 0; i < remaining; i++) {
        agent->messages[1 + i] = agent->messages[1 + drop_count + i];
    }
    agent->msg_count = 1 + remaining;
}

void c_agent_clear_history(CAgent *agent) {
    if (!agent || agent->msg_count <= 1) return;
    for (size_t i = 1; i < agent->msg_count; i++) {
        free_single_message(&agent->messages[i]);
    }
    agent->msg_count = 1;
}

size_t c_agent_total_tokens(const CAgent *agent) {
    if (!agent) return 0;
    size_t total = 0;
    for (size_t i = 0; i < agent->msg_count; i++) {
        total += 4; // overhead per message
        if (agent->messages[i].content) {
            total += count_estimated_tokens(agent->messages[i].content);
        }
        if (agent->messages[i].tool_calls) {
            for (size_t k = 0; k < agent->messages[i].tool_call_count; k++) {
                if (agent->messages[i].tool_calls[k].arguments_json) {
                    total += count_estimated_tokens(agent->messages[i].tool_calls[k].arguments_json);
                }
            }
        }
    }
    return total;
}

bool c_agent_save_session(CAgent *agent, const char *session_id, const char *title) {
    if (!agent || !agent->db || !session_id || strlen(session_id) == 0) return false;

    // Upsert session metadata
    sqlite3_stmt *stmt;
    const char *sess_sql = "INSERT INTO sessions (id, title, model, updated_at) VALUES (?, ?, ?, CURRENT_TIMESTAMP) "
                           "ON CONFLICT(id) DO UPDATE SET title = excluded.title, model = excluded.model, updated_at = CURRENT_TIMESTAMP;";
    if (sqlite3_prepare_v2(agent->db, sess_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, title ? title : session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, agent->gateway->model, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete existing session messages
    const char *del_sql = "DELETE FROM session_messages WHERE session_id = ?;";
    if (sqlite3_prepare_v2(agent->db, del_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Insert current messages
    const char *ins_sql = "INSERT INTO session_messages (session_id, idx, role, content, tool_call_id, tool_calls_json) VALUES (?, ?, ?, ?, ?, ?);";
    for (size_t i = 0; i < agent->msg_count; i++) {
        if (sqlite3_prepare_v2(agent->db, ins_sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 2, (int)i);
            sqlite3_bind_text(stmt, 3, agent->messages[i].role, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, agent->messages[i].content ? agent->messages[i].content : "", -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 5, agent->messages[i].tool_call_id ? agent->messages[i].tool_call_id : "", -1, SQLITE_STATIC);

            char *tc_json = NULL;
            if (agent->messages[i].tool_calls && agent->messages[i].tool_call_count > 0) {
                JsonValue *arr = json_create_array();
                for (size_t k = 0; k < agent->messages[i].tool_call_count; k++) {
                    JsonValue *tc_obj = json_create_object();
                    json_obj_add(tc_obj, "id", json_create_string(agent->messages[i].tool_calls[k].id));
                    json_obj_add(tc_obj, "name", json_create_string(agent->messages[i].tool_calls[k].name));
                    json_obj_add(tc_obj, "arguments", json_create_string(agent->messages[i].tool_calls[k].arguments_json));
                    json_arr_add(arr, tc_obj);
                }
                tc_json = json_serialize(arr);
                json_free(arr);
            }

            sqlite3_bind_text(stmt, 6, tc_json ? tc_json : "", -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (tc_json) free(tc_json);
        }
    }

    char save_summary[256];
    snprintf(save_summary, sizeof(save_summary), "Session '%s' saved (%zu messages)", session_id, agent->msg_count);
    c_agent_log_timeline(agent, "session_saved", save_summary);

    return true;
}

bool c_agent_load_session(CAgent *agent, const char *session_id) {
    if (!agent || !agent->db || !session_id) return false;

    sqlite3_stmt *stmt;
    const char *sql = "SELECT idx, role, content, tool_call_id, tool_calls_json FROM session_messages WHERE session_id = ? ORDER BY idx ASC;";
    if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_STATIC);

    // Temporarily collect messages
    size_t count = 0;
    size_t cap = 32;
    AgentMessage *loaded = calloc(cap, sizeof(AgentMessage));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= cap) {
            cap *= 2;
            AgentMessage *more = realloc(loaded, sizeof(AgentMessage) * cap);
            if (!more) { sqlite3_finalize(stmt); return false; }
            loaded = more;
        }

        const char *role = (const char *)sqlite3_column_text(stmt, 1);
        const char *content = (const char *)sqlite3_column_text(stmt, 2);
        const char *t_id = (const char *)sqlite3_column_text(stmt, 3);
        const char *tc_json = (const char *)sqlite3_column_text(stmt, 4);

        AgentMessage *m = &loaded[count++];
        memset(m, 0, sizeof(AgentMessage));
        m->role = strdup(role ? role : "user");
        m->content = strdup(content ? content : "");
        if (t_id && strlen(t_id) > 0) m->tool_call_id = strdup(t_id);

        if (tc_json && strlen(tc_json) > 0) {
            JsonValue *tc_arr = json_parse(tc_json);
            if (tc_arr && tc_arr->type == JSON_ARRAY && tc_arr->u.array.count > 0) {
                m->tool_call_count = tc_arr->u.array.count;
                m->tool_calls = calloc(m->tool_call_count, sizeof(ModelParsedToolCall));
                for (size_t k = 0; k < m->tool_call_count; k++) {
                    JsonValue *tc_o = tc_arr->u.array.items[k];
                    const char *tid = json_obj_get_str(tc_o, "id");
                    const char *tname = json_obj_get_str(tc_o, "name");
                    const char *targs = json_obj_get_str(tc_o, "arguments");
                    m->tool_calls[k].id = strdup(tid ? tid : "");
                    m->tool_calls[k].name = strdup(tname ? tname : "");
                    m->tool_calls[k].arguments_json = strdup(targs ? targs : "{}");
                }
            }
            if (tc_arr) json_free(tc_arr);
        }
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        free(loaded);
        return false;
    }

    // Free existing messages in agent
    for (size_t i = 0; i < agent->msg_count; i++) {
        free_single_message(&agent->messages[i]);
    }
    free(agent->messages);

    agent->messages = loaded;
    agent->msg_count = count;
    agent->msg_cap = cap;

    char load_summary[256];
    snprintf(load_summary, sizeof(load_summary), "Session '%s' restored (%zu messages)", session_id, count);
    c_agent_log_timeline(agent, "session_loaded", load_summary);

    return true;
}

char *c_agent_list_sessions(CAgent *agent) {
    if (!agent || !agent->db) return strdup("Session store not active.");

    sqlite3_stmt *stmt;
    const char *sql = "SELECT s.id, s.title, s.model, s.updated_at, COUNT(m.id) "
                      "FROM sessions s LEFT JOIN session_messages m ON s.id = m.session_id "
                      "GROUP BY s.id ORDER BY s.updated_at DESC LIMIT 15;";
    if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return strdup("Failed to query sessions table.");
    }

    DynString ds = dyn_str_new();
    dyn_str_append(&ds, "=== Saved Sessions ===\n");
    size_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        const char *title = (const char *)sqlite3_column_text(stmt, 1);
        const char *model = (const char *)sqlite3_column_text(stmt, 2);
        const char *updated = (const char *)sqlite3_column_text(stmt, 3);
        int msgs = sqlite3_column_int(stmt, 4);

        dyn_str_appendf(&ds, "• [%s] \"%s\" (%d msgs) | Model: %s | Saved: %s\n",
            id ? id : "", title ? title : "", msgs, model ? model : "", updated ? updated : "");
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) dyn_str_append(&ds, "No saved sessions found. Use /save <session_id> to checkpoint.");
    return ds.data;
}

char *c_agent_reflect_and_distill(CAgent *agent) {
    if (!agent || agent->msg_count <= 2) {
        return strdup("Not enough conversation turns to distill a reusable skill.");
    }

    DynString skill = dyn_str_new();
    dyn_str_append(&skill, "Distilled Workflow Trajectory:\n");
    int tool_uses = 0;

    for (size_t i = 1; i < agent->msg_count; i++) {
        if (agent->messages[i].tool_calls && agent->messages[i].tool_call_count > 0) {
            for (size_t k = 0; k < agent->messages[i].tool_call_count; k++) {
                dyn_str_appendf(&skill, "Step: Tool %s with arguments: %s\n",
                    agent->messages[i].tool_calls[k].name,
                    agent->messages[i].tool_calls[k].arguments_json);
                tool_uses++;
            }
        }
    }

    if (tool_uses == 0) {
        dyn_str_free(&skill);
        return strdup("No tool actions detected in recent turns to distill.");
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "Skill (%s)", agent->messages[1].content ? agent->messages[1].content : "workflow");
    if (strlen(topic) > 60) {
        topic[57] = '.'; topic[58] = '.'; topic[59] = '.'; topic[60] = '\0';
    }

    c_agent_persist_memory(agent, topic, skill.data);
    dyn_str_free(&skill);

    c_agent_log_timeline(agent, "skill_distilled", topic);

    DynString res = dyn_str_new();
    dyn_str_appendf(&res, "Successfully distilled and indexed skill into SQLite FTS5 memory under topic: '%s'", topic);
    return res.data;
}

ModelGatewayResponse c_agent_step(CAgent *agent) {
    // Auto-save: save before compaction drops messages, and on turn interval
    if (agent->auto_save_interval > 0 && agent->db) {
        bool should_save = false;
        if (agent->msg_count > 1) {
            if (agent->max_context_messages > 0 && agent->msg_count > agent->max_context_messages)
                should_save = true;
            size_t est_tokens = c_agent_total_tokens(agent);
            size_t budget = agent->max_context_tokens > 0 ? agent->max_context_tokens : 128000;
            if (est_tokens > (budget * 80 / 100) && agent->msg_count > 10)
                should_save = true;
        }
        if (agent->turns_since_save >= agent->auto_save_interval)
            should_save = true;
        if (should_save) {
            time_t now = time(NULL);
            struct tm *tm = gmtime(&now);
            char sid[96];
            strftime(sid, sizeof(sid), "auto_%Y%m%d_%H%M%S", tm);
            const char *title = NULL;
            for (size_t i = 1; i < agent->msg_count; i++) {
                if (agent->messages[i].role && strcmp(agent->messages[i].role, "user") == 0 &&
                    agent->messages[i].content && strlen(agent->messages[i].content) > 0) {
                    title = agent->messages[i].content;
                    break;
                }
            }
            char short_title[128];
            if (title) {
                size_t tlen = strlen(title);
                if (tlen > 100) { memcpy(short_title, title, 97); short_title[97] = '.'; short_title[98] = '.'; short_title[99] = '.'; short_title[100] = '\0'; }
                else { snprintf(short_title, sizeof(short_title), "%s", title); }
            } else {
                snprintf(short_title, sizeof(short_title), "Auto-save turn %zu", agent->turn_count);
            }
            if (c_agent_save_session(agent, sid, short_title)) {
                char auto_msg[96];
                snprintf(auto_msg, sizeof(auto_msg), "Session auto-saved (turn %zu)", agent->turn_count);
                c_agent_log_timeline(agent, "auto_save", auto_msg);
            }
            agent->turns_since_save = 0;
        }
    }
    // Check if context window auto-pruning or token budget compaction is needed
    if (agent->msg_count > 1) {
        if (agent->max_context_messages > 0 && agent->msg_count > agent->max_context_messages) {
            size_t keep = agent->max_context_messages > 20 ? 20 : agent->max_context_messages / 2;
            c_agent_compact_history(agent, keep);
        }
        size_t est_tokens = c_agent_total_tokens(agent);
        size_t budget = agent->max_context_tokens > 0 ? agent->max_context_tokens : 128000;
        if (est_tokens > (budget * 80 / 100) && agent->msg_count > 10) {
            c_agent_compact_history(agent, 10);
            c_agent_log_timeline(agent, "auto_compaction", "Compacted context to 10 messages (80% token budget reached)");
        }
    }

    // 1. Build messages array
    JsonValue *messages_arr = json_create_array();
    for (size_t i = 0; i < agent->msg_count; i++) {
        JsonValue *m = json_create_object();
        json_obj_add(m, "role", json_create_string(agent->messages[i].role));
        json_obj_add(m, "content", json_create_string(agent->messages[i].content));
        if (agent->messages[i].tool_call_id) {
            json_obj_add(m, "tool_call_id", json_create_string(agent->messages[i].tool_call_id));
        }

        // Prompt Caching breakpoint on system instruction
        if (i == 0 && agent->gateway && agent->gateway->prompt_caching) {
            JsonValue *cc = json_create_object();
            json_obj_add(cc, "type", json_create_string("ephemeral"));
            json_obj_add(m, "cache_control", cc);
        }

        if (agent->messages[i].tool_calls && agent->messages[i].tool_call_count > 0) {
            JsonValue *tcs = json_create_array();
            for (size_t k = 0; k < agent->messages[i].tool_call_count; k++) {
                JsonValue *tc = json_create_object();
                json_obj_add(tc, "id", json_create_string(agent->messages[i].tool_calls[k].id));
                json_obj_add(tc, "type", json_create_string("function"));
                JsonValue *fn = json_create_object();
                json_obj_add(fn, "name", json_create_string(agent->messages[i].tool_calls[k].name));
                json_obj_add(fn, "arguments", json_create_string(agent->messages[i].tool_calls[k].arguments_json));
                json_obj_add(tc, "function", fn);
                json_arr_add(tcs, tc);
            }
            json_obj_add(m, "tool_calls", tcs);
        }
        json_arr_add(messages_arr, m);
    }

    // 2. Build Tool definitions schema
    JsonValue *tools_arr = json_create_array();
    for (size_t i = 0; i < agent->schema_count; i++) {
        JsonValue *t = json_create_object();
        json_obj_add(t, "type", json_create_string("function"));
        JsonValue *fn = json_create_object();
        json_obj_add(fn, "name", json_create_string(agent->schemas[i].name));
        json_obj_add(fn, "description", json_create_string(agent->schemas[i].description));
        json_obj_add(fn, "parameters", agent->schemas[i].parameters_schema);
        json_obj_add(t, "function", fn);

        // Prompt Caching breakpoint on last tool schema
        if (i == agent->schema_count - 1 && agent->gateway && agent->gateway->prompt_caching) {
            JsonValue *cc = json_create_object();
            json_obj_add(cc, "type", json_create_string("ephemeral"));
            json_obj_add(t, "cache_control", cc);
        }

        json_arr_add(tools_arr, t);
    }

    ModelGatewayResponse resp = agent->gateway->chat_complete(agent->gateway, messages_arr, tools_arr);

    // Detach shared references before freeing arrays
    for (size_t i = 0; i < tools_arr->u.array.count; i++) {
        JsonValue *t = tools_arr->u.array.items[i];
        JsonValue *fn = json_obj_get(t, "function");
        if (fn) {
            for (size_t m = 0; m < fn->u.object.count; m++) {
                if (fn->u.object.members[m].key && strcmp(fn->u.object.members[m].key, "parameters") == 0) {
                    fn->u.object.members[m].value = NULL;
                }
            }
        }
    }
    json_free(tools_arr);
    json_free(messages_arr);

    // Append Assistant response to history
    if (agent->msg_count >= agent->msg_cap) {
        agent->msg_cap *= 2;
        AgentMessage *new_msgs = realloc(agent->messages, sizeof(AgentMessage) * agent->msg_cap);
        if (!new_msgs) {
            fprintf(stderr, "[Fatal] Out of memory in c_agent_step\n");
            abort();
        }
        agent->messages = new_msgs;
    }
    AgentMessage *ast_msg = &agent->messages[agent->msg_count++];
    memset(ast_msg, 0, sizeof(AgentMessage));
    ast_msg->role = strdup("assistant");
    ast_msg->content = strdup(resp.content ? resp.content : "");
    if (resp.has_tool_call) {
        ast_msg->tool_call_count = resp.tool_call_count;
        ast_msg->tool_calls = calloc(resp.tool_call_count, sizeof(ModelParsedToolCall));
        for (size_t i = 0; i < resp.tool_call_count; i++) {
            ast_msg->tool_calls[i].id = strdup(resp.tool_calls[i].id);
            ast_msg->tool_calls[i].name = strdup(resp.tool_calls[i].name);
            ast_msg->tool_calls[i].arguments_json = strdup(resp.tool_calls[i].arguments_json);
        }
    }

    agent->turn_count++;
    agent->turns_since_save++;

    return resp;
}

void c_agent_set_auto_save_interval(CAgent *agent, size_t interval) {
    if (agent) {
        agent->auto_save_interval = interval;
        agent->turns_since_save = 0;
    }
}

void c_agent_free(CAgent *agent) {
    if (!agent) return;
    if (agent->db) {
        sqlite3_wal_checkpoint_v2(agent->db, NULL, SQLITE_CHECKPOINT_TRUNCATE, NULL, NULL);
        sqlite3_close(agent->db);
    }
    for (size_t i = 0; i < agent->msg_count; i++) {
        free_single_message(&agent->messages[i]);
    }
    free(agent->messages);

    for (size_t i = 0; i < agent->schema_count; i++) {
        free(agent->schemas[i].name);
        free(agent->schemas[i].description);
        json_free(agent->schemas[i].parameters_schema);
    }
    free(agent->schemas);
    free(agent);
}
