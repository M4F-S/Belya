#ifndef BELYA_COMPANY_H
#define BELYA_COMPANY_H

#include "belya_harness.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define COMPANY_MAX_ROLES 32
#define COMPANY_MAX_TASKS 64

typedef enum {
    COMPANY_PHASE_INIT = 0,
    COMPANY_PHASE_COMMERCIAL,      // Account Executive, Solutions Engineer, Customer Success
    COMPANY_PHASE_PRODUCT_DESIGN,  // Product Manager, UI/UX Designer
    COMPANY_PHASE_ENGINEERING,     // Frontend, Backend, Full-Stack, Data Engineer
    COMPANY_PHASE_SECURITY_QA,     // Cybersecurity Engineer, QA/Test Engineer
    COMPANY_PHASE_DEPLOY_DOCS,     // DevOps/Cloud Engineer, Technical Writer
    COMPANY_PHASE_DELIVERY_REVIEW, // Customer Delivery Director, Customer Sign-off
    COMPANY_PHASE_COMPLETED,
    COMPANY_PHASE_FAILED
} CompanyPhase;

typedef struct {
    char name[64];
    char role[128];
    char tools[512];
    int max_turns;
    bool active;
    time_t last_dispatch_time;
    size_t total_tasks_executed;
} CompanyAgentStatus;

typedef struct {
    int64_t project_id;
    char client_name[128];
    char mission[512];
    CompanyPhase current_phase;
    time_t start_time;
    time_t end_time;
    int total_tasks;
    int completed_tasks;
    bool security_cleared;
    bool qa_passed;
    bool client_accepted;
} CompanyProject;

typedef struct {
    BelyaHarness *harness;
    sqlite3 *company_db;
    char db_path[512];
    CompanyAgentStatus roster[COMPANY_MAX_ROLES];
    size_t roster_count;
    CompanyProject active_project;
    bool has_active_project;

    // Supervisory State
    double team_vmrss_mb;
    int consecutive_circuit_trips;
    size_t synthesized_skills_count;
    size_t synthesized_tools_count;
} BelyaCompany;

// Lifecycle
BelyaCompany *belya_company_init(BelyaHarness *harness, const char *db_path);
void belya_company_free(BelyaCompany *company);

// Roster & Discovery
size_t belya_company_load_roster(BelyaCompany *company, const char *agents_dir);
const CompanyAgentStatus *belya_company_get_agent(BelyaCompany *company, const char *role_name);
void belya_company_print_roster(BelyaCompany *company);

// Project & SDLC Execution
bool belya_company_start_project(BelyaCompany *company, const char *client_name, const char *mission);
bool belya_company_execute_phase(BelyaCompany *company, CompanyPhase phase);
char *belya_company_run_sdlc(BelyaCompany *company, const char *client_name, const char *mission);

// Supervisory Meta-Agents
char *belya_company_monitor_health(BelyaCompany *company); // Capability Evolution Officer (Almaz Core)
char *belya_company_generate_customer_digest(BelyaCompany *company); // Customer Delivery Director

// CLI & REPL Dispatch
char *belya_company_handle_command(BelyaHarness *harness, const char *cmd_args);

// Helpers
const char *belya_company_phase_name(CompanyPhase phase);

#endif // BELYA_COMPANY_H
