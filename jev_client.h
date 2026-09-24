#ifndef JEV_CLIENT_H
#define JEV_CLIENT_H

#include "common.h"
#include "minijson.h"
#include <stdbool.h>

typedef struct {
    char *choice;
    double confidence;
} JevChoiceResult;

typedef struct {
    char *action;      /* "allow", "confirm", "block" */
    double risk;       /* 0.0 - 1.0 */
    double confidence;
} JevRiskResult;

typedef struct {
    char *action;      /* "keep", "truncate", "drop" */
    char *relevance;
    bool redundant;
    double confidence;
} JevContextFilterResult;

typedef struct {
    char *api_key;
    char *base_url;
    long timeout_ms;
} JevClient;

JevClient *jev_client_init(const char *api_key);
void jev_client_free(JevClient *client);

/* 1. Fast Subagent Triage Routing (returns allocated role name, caller frees) */
char *jev_decide_triage_role(JevClient *client, const char *user_prompt);

/* 2. Tool-call Safety Gating */
JevRiskResult *jev_check_tool_risk(JevClient *client, const char *goal, const char *tool, const char *arguments);
void jev_risk_result_free(JevRiskResult *res);

/* 3. Context Filtering (Keep / Truncate / Drop) */
JevContextFilterResult *jev_filter_context_item(JevClient *client, const char *task, const char *item_text);
void jev_context_filter_result_free(JevContextFilterResult *res);

/* Parsing helpers (exposed for testing and offline validation) */
char *jev_parse_triage_response(const char *json_str);
JevRiskResult *jev_parse_risk_response(const char *json_str);
JevContextFilterResult *jev_parse_filter_response(const char *json_str);

#endif
