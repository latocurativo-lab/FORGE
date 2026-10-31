/* Unit tests for forge_parse_context_fields: continuity history replay and
   Studio project-instructions parsing on the JSONL start line. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int forge_parse_context_fields(const char *line, char **history_json, char **instructions);
int forge_task_requests_workspace_change(const char *task);

static void reset(char **history, char **instructions) {
    free(*history); free(*instructions);
    *history = NULL; *instructions = NULL;
}

int main(void) {
    char *history = NULL, *instructions = NULL;

    assert(forge_task_requests_workspace_change("ottimo, fai un piano ed eseguilo."));
    assert(forge_task_requests_workspace_change("crea un CMS in Next.js"));
    assert(forge_task_requests_workspace_change("realiziamo un cms in next js"));
    assert(forge_task_requests_workspace_change("realizziamo un CMS in Next.js"));
    assert(forge_task_requests_workspace_change("va bene così, procedi"));
    assert(forge_task_requests_workspace_change("creala"));
    assert(forge_task_requests_workspace_change("riprova"));
    assert(forge_task_requests_workspace_change("riprovaci adesso"));
    assert(forge_task_requests_workspace_change("ritenta"));
    assert(!forge_task_requests_workspace_change("Ciao, come stai?"));
    assert(!forge_task_requests_workspace_change("come si crea un CMS in Next.js?"));

    /* Absent fields are valid uses of neither. */
    assert(forge_parse_context_fields("{\"type\":\"start\",\"task\":\"t\",\"workspace\":\"w\"}", &history, &instructions) == 1);
    assert(history == NULL && instructions == NULL);
    reset(&history, &instructions);

    /* Empty history array is valid and produces no replay payload. */
    assert(forge_parse_context_fields("{\"history\":[],\"instructions\":\"\"}", &history, &instructions) == 1);
    assert(history == NULL && instructions == NULL);
    reset(&history, &instructions);

    /* A valid multi-turn history is canonicalized. */
    assert(forge_parse_context_fields(
        "{\"history\":[{\"role\":\"user\",\"content\":\"ciao\"},{\"role\":\"assistant\",\"content\":\"salve\"},{\"role\":\"user\",\"content\":\"come si chiama FORGE?\"}],\"instructions\":\"usa nomi brevi\"}",
        &history, &instructions) == 1);
    assert(history && strstr(history, "come si chiama FORGE?"));
    assert(strstr(history, "\"role\":\"user\"") && strstr(history, "\"role\":\"assistant\"") && strstr(history, "salve"));
    assert(instructions && strcmp(instructions, "usa nomi brevi") == 0);
    reset(&history, &instructions);

    /* Invalid roles, empty content and bad shapes must fail closed. */
    assert(forge_parse_context_fields("{\"history\":[{\"role\":\"system\",\"content\":\"x\"}]}", &history, &instructions) == 0);
    assert(history == NULL && instructions == NULL);
    assert(forge_parse_context_fields("{\"history\":[{\"role\":\"user\",\"content\":\"\"}]}", &history, &instructions) == 0);
    assert(forge_parse_context_fields("{\"history\":[\"user\"]}", &history, &instructions) == 0);
    assert(forge_parse_context_fields("{\"history\":\"full\"}", &history, &instructions) == 0);
    /* instructions must be a string when present. */
    assert(forge_parse_context_fields("{\"instructions\":5}", &history, &instructions) == 0);

    /* >64 history items rejected. */
    {
        char big[4096]; size_t off = (size_t)snprintf(big, sizeof(big), "{\"history\":[");
        for (size_t i = 0; off < sizeof(big) - 64 && i < 65; i++)
            off += (size_t)snprintf(big + off, sizeof(big) - off, "%s{\"role\":\"user\",\"content\":\"m%zu\"}", i ? "," : "", i);
        snprintf(big + off, sizeof(big) - off, "]}");
        assert(forge_parse_context_fields(big, &history, &instructions) == 0);
        assert(history == NULL && instructions == NULL);
    }
    reset(&history, &instructions);
    printf("forge_parse_context_fields tests passed\n");
    return 0;
}
