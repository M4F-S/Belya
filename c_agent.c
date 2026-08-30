#include "c_agent.h"

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

    // Initialize SQLite memory store for Hermes skills and reflections
    if (sqlite3_open(db_path, &agent->db) == SQLITE_OK) {
        const char *schema_sql = 
            "CREATE TABLE IF NOT EXISTS agent_memory ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  topic TEXT NOT NULL,"
            "  content TEXT NOT NULL,"
            "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";
        sqlite3_exec(agent->db, schema_sql, 0, 0, 0);

        // Check if FTS5 is available
        const char *fts_sql = "CREATE VIRTUAL TABLE IF NOT EXISTS agent_memory_fts USING fts5(topic, content);";
        if (sqlite3_exec(agent->db, fts_sql, 0, 0, 0) == SQLITE_OK) {
            agent->has_fts5 = true;
        }
    }

    // Set Hermes System Core
    DynString sys = dyn_str_new();
    dyn_str_append(&sys, system_instructions);
    dyn_str_append(&sys, "\nYou are C Agent, an autonomous software engine. Use your tools sequentially to solve tasks.");
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

void c_agent_persist_memory(CAgent *agent, const char *topic, const char *content) {
    if (!agent->db) return;
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO agent_memory (topic, content) VALUES (?, ?);";
    if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, topic, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, content, -1, SQLITE_STATIC);
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
}

char *c_agent_search_memory(CAgent *agent, const char *query) {
    if (!agent->db) return strdup("Memory database not active.");
    DynString out = dyn_str_new();

    // Try FTS5 first if enabled
    if (agent->has_fts5 && query && strlen(query) > 0) {
        sqlite3_stmt *stmt;
        const char *sql = "SELECT topic, content FROM agent_memory_fts WHERE agent_memory_fts MATCH ? ORDER BY rank LIMIT 5;";
        if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *top = (const char *)sqlite3_column_text(stmt, 0);
                const char *txt = (const char *)sqlite3_column_text(stmt, 1);
                dyn_str_append(&out, "=== Skill/Memory Entry (FTS5 Match) ===\nTopic: ");
                dyn_str_append(&out, top ? top : "");
                dyn_str_append(&out, "\nKnowledge: ");
                dyn_str_append(&out, txt ? txt : "");
                dyn_str_append(&out, "\n\n");
            }
            sqlite3_finalize(stmt);
        }
    }

    // Fallback to LIKE query if FTS5 produced no results
    if (out.len == 0) {
        sqlite3_stmt *stmt;
        const char *sql = "SELECT topic, content FROM agent_memory WHERE topic LIKE ? OR content LIKE ? LIMIT 5;";
        if (sqlite3_prepare_v2(agent->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            char pattern[256];
            snprintf(pattern, sizeof(pattern), "%%%s%%", query ? query : "");
            sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *top = (const char *)sqlite3_column_text(stmt, 0);
                const char *txt = (const char *)sqlite3_column_text(stmt, 1);
                dyn_str_append(&out, "=== Skill/Memory Entry ===\nTopic: ");
                dyn_str_append(&out, top ? top : "");
                dyn_str_append(&out, "\nKnowledge: ");
                dyn_str_append(&out, txt ? txt : "");
                dyn_str_append(&out, "\n\n");
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

    // Shift remaining messages down (preserving messages[0] which is system)
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

ModelGatewayResponse c_agent_step(CAgent *agent) {
    // Check if context window auto-pruning is needed
    if (agent->max_context_messages > 0 && agent->msg_count > agent->max_context_messages) {
        size_t keep = agent->max_context_messages > 20 ? 20 : agent->max_context_messages / 2;
        c_agent_compact_history(agent, keep);
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

    return resp;
}

void c_agent_free(CAgent *agent) {
    if (!agent) return;
    if (agent->db) sqlite3_close(agent->db);
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
