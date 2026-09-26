/*
 * positron_app/app_forms.h - private Core form/default-action adapter.
 *
 * Core owns form ownership, validation, successful-control selection and
 * multipart encoding.  This module only turns one Core snapshot into the
 * request description that the WM6 host can queue or send.
 */

#ifndef POSITRON_APP_FORMS_H
#define POSITRON_APP_FORMS_H

#include <windows.h>

#include "positron_core.h"
#include "positron_browser.h"

#define APP_FORMS_URL_MAX            1024
#define APP_FORMS_CONTENT_TYPE_MAX  256
#define APP_FORMS_BODY_MAX_BYTES    (1 * 1024 * 1024)
#define APP_FORMS_DIALOG_VALUE_MAX  PBROWSER_SCRIPT_DIALOG_VALUE_MAX

#define APP_FORMS_RESULT_NONE        0
#define APP_FORMS_RESULT_OK          1
#define APP_FORMS_RESULT_INVALID     2
#define APP_FORMS_RESULT_UNSUPPORTED 3
#define APP_FORMS_RESULT_FAILED      4

typedef int (*AppFormsResolveUrlFn)(void *pw, const char *base_url,
        const char *reference, char *output, int output_capacity);

typedef struct AppFormsAdapter {
    unsigned long size;
    void *pw;
    AppFormsResolveUrlFn resolve_url;
    PCoreMultipartFileReadFn read_file;
    PCoreMultipartFileFreeFn free_file;
    void *file_pw;
} AppFormsAdapter;

typedef struct AppFormRequest {
    int valid;
    int method;
    char target_url[APP_FORMS_URL_MAX];
    char *body;
    int body_bytes;
    char content_type[APP_FORMS_CONTENT_TYPE_MAX];
    char dialog_id[PBROWSER_SCRIPT_DIALOG_ID_MAX];
    char return_value[APP_FORMS_DIALOG_VALUE_MAX];
} AppFormRequest;

void AppForms_InitRequest(AppFormRequest *request);
void AppForms_ClearRequest(AppFormRequest *request);

int AppForms_BuildAt(HANDLE document, const char *document_url, int x, int y,
        const AppFormsAdapter *adapter, AppFormRequest *out_request);
int AppForms_BuildForTextInput(HANDLE document, const char *document_url,
        unsigned int text_index, const AppFormsAdapter *adapter,
        AppFormRequest *out_request);
int AppForms_BuildById(HANDLE document, const char *document_url,
        const char *form_id, const char *submitter_id, int validate,
        const AppFormsAdapter *adapter, AppFormRequest *out_request);

#endif /* POSITRON_APP_FORMS_H */
