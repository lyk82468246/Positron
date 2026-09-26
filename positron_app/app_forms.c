/*
 * positron_app/app_forms.c - private Core form/default-action adapter.
 */

#include <stdlib.h>
#include <string.h>

#include "app_forms.h"

typedef struct AppFormsSubmissionData {
    int source;
    int x;
    int y;
    unsigned int text_index;
    const char *form_id;
    const char *submitter_id;
    int validate;
} AppFormsSubmissionData;

#define APP_FORMS_SOURCE_AT       1
#define APP_FORMS_SOURCE_TEXT     2
#define APP_FORMS_SOURCE_BY_ID    3
#define APP_FORMS_SOURCE_DIRECT   4

static void app_forms_copy_text(char *target, int capacity,
        const char *source)
{
    int length;

    if (target == NULL || capacity <= 0) {
        return;
    }
    target[0] = '\0';
    if (source == NULL || capacity == 1) {
        return;
    }
    length = (int) strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
    }
    memcpy(target, source, (size_t) length);
    target[length] = '\0';
}

void AppForms_InitRequest(AppFormRequest *request)
{
    if (request != NULL) {
        memset(request, 0, sizeof(*request));
    }
}

void AppForms_ClearRequest(AppFormRequest *request)
{
    if (request == NULL) {
        return;
    }
    free(request->body);
    memset(request, 0, sizeof(*request));
}

static int app_forms_submission(const AppFormsSubmissionData *data,
        HANDLE document, PCoreFormSubmissionInfo *info, char *action,
        int action_capacity, char *body, int body_capacity)
{
    if (data == NULL || document == NULL || info == NULL) {
        return 0;
    }
    if (data->source == APP_FORMS_SOURCE_AT) {
        return PCore_FormSubmissionAt(document, data->x, data->y, info,
                action, action_capacity, body, body_capacity);
    }
    if (data->source == APP_FORMS_SOURCE_TEXT) {
        return PCore_FormSubmissionForTextInput(document, data->text_index,
                info, action, action_capacity, body, body_capacity);
    }
    if (data->source == APP_FORMS_SOURCE_BY_ID) {
        if (data->validate) {
            return PCore_FormSubmissionById(document, data->form_id,
                    data->submitter_id, info, action, action_capacity, body,
                    body_capacity);
        }
        return PCore_FormSubmissionNoValidationById(document,
                data->form_id, info, action, action_capacity, body,
                body_capacity);
    }
    if (data->source == APP_FORMS_SOURCE_DIRECT) {
        return PCore_FormSubmissionNoValidationById(document,
                data->form_id, info, action, action_capacity, body,
                body_capacity);
    }
    return 0;
}

static int app_forms_dialog_submission(const AppFormsSubmissionData *data,
        HANDLE document, PCoreDialogFormSubmissionInfo *info, char *dialog_id,
        int dialog_id_capacity, char *return_value,
        int return_value_capacity)
{
    if (data == NULL || document == NULL || info == NULL) {
        return 0;
    }
    if (data->source == APP_FORMS_SOURCE_AT) {
        return PCore_FormDialogSubmissionAt(document, data->x, data->y,
                info, dialog_id, dialog_id_capacity, return_value,
                return_value_capacity);
    }
    if (data->source == APP_FORMS_SOURCE_TEXT) {
        return PCore_FormDialogSubmissionForTextInput(document,
                data->text_index, info, dialog_id, dialog_id_capacity,
                return_value, return_value_capacity);
    }
    if (data->source == APP_FORMS_SOURCE_BY_ID) {
        if (data->validate) {
            return PCore_FormDialogSubmissionById(document, data->form_id,
                    data->submitter_id, info, dialog_id, dialog_id_capacity,
                    return_value, return_value_capacity);
        }
        return PCore_FormDialogSubmissionNoValidationById(document,
                data->form_id, info, dialog_id, dialog_id_capacity,
                return_value, return_value_capacity);
    }
    if (data->source == APP_FORMS_SOURCE_DIRECT) {
        return PCore_FormDialogSubmissionNoValidationById(document,
                data->form_id, info, dialog_id, dialog_id_capacity,
                return_value, return_value_capacity);
    }
    return 0;
}

static HANDLE app_forms_multipart_submission(
        const AppFormsSubmissionData *data, HANDLE document)
{
    if (data == NULL || document == NULL) {
        return NULL;
    }
    if (data->source == APP_FORMS_SOURCE_AT) {
        return PCore_MultipartSubmissionAt(document, data->x, data->y);
    }
    if (data->source == APP_FORMS_SOURCE_TEXT) {
        return PCore_MultipartSubmissionForTextInput(document,
                data->text_index);
    }
    if (data->source == APP_FORMS_SOURCE_BY_ID) {
        if (data->validate) {
            return PCore_MultipartSubmissionById(document, data->form_id,
                    data->submitter_id);
        }
        return PCore_MultipartSubmissionNoValidationById(document,
                data->form_id);
    }
    if (data->source == APP_FORMS_SOURCE_DIRECT) {
        return PCore_MultipartSubmissionNoValidationById(document,
                data->form_id);
    }
    return NULL;
}

static int app_forms_resolve_target(const char *document_url,
        const char *action, const char *body, int method,
        const AppFormsAdapter *adapter, char *target, int target_capacity)
{
    char resolved[APP_FORMS_URL_MAX];
    const char *effective_action;
    const char *query;
    int resolved_length;
    int body_length;
    int target_length;
    int separator_length;

    if (document_url == NULL || document_url[0] == '\0' || adapter == NULL ||
            adapter->resolve_url == NULL || target == NULL ||
            target_capacity <= 1 || (body == NULL && method ==
            PCORE_FORM_METHOD_GET)) {
        return 1;
    }
    effective_action = (action != NULL && action[0] != '\0') ? action :
            document_url;
    if (adapter->resolve_url(adapter->pw, document_url, effective_action,
            resolved, sizeof(resolved)) != 0) {
        return 1;
    }
    if (method != PCORE_FORM_METHOD_GET) {
        if ((int) strlen(resolved) >= target_capacity) {
            return 1;
        }
        app_forms_copy_text(target, target_capacity, resolved);
        return 0;
    }
    resolved_length = (int) strlen(resolved);
    body_length = (int) strlen(body);
    query = strchr(resolved, '?');
    separator_length = body_length > 0 ? (query == NULL ||
            query[1] != '\0' ? 1 : 0) : 0;
    target_length = resolved_length + body_length + separator_length;
    if (resolved_length <= 0 || target_length >= target_capacity) {
        return 1;
    }
    memcpy(target, resolved, (size_t) resolved_length);
    if (body_length > 0) {
        if (separator_length != 0) {
            target[resolved_length] = query == NULL ? '?' : '&';
        }
        memcpy(target + resolved_length + separator_length, body,
                (size_t) body_length);
    }
    target[target_length] = '\0';
    return 0;
}

static int app_forms_copy_body(AppFormRequest *request, const char *body,
        int body_bytes)
{
    if (request == NULL || body_bytes < 0 || body_bytes >
            APP_FORMS_BODY_MAX_BYTES) {
        return 1;
    }
    request->body = (char *) malloc(body_bytes > 0 ?
            (size_t) body_bytes : 1U);
    if (request->body == NULL) {
        return 1;
    }
    if (body_bytes > 0 && body != NULL) {
        memcpy(request->body, body, (size_t) body_bytes);
    }
    request->body_bytes = body_bytes;
    return 0;
}

static int app_forms_build_dialog(const AppFormsSubmissionData *data,
        HANDLE document, const char *document_url,
        const AppFormsAdapter *adapter, AppFormRequest *request)
{
    PCoreDialogFormSubmissionInfo info;
    char dialog_probe[1];
    char value_probe[1];
    int result;

    memset(&info, 0, sizeof(info));
    dialog_probe[0] = '\0';
    value_probe[0] = '\0';
    result = app_forms_dialog_submission(data, document, &info,
            dialog_probe, sizeof(dialog_probe), value_probe,
            sizeof(value_probe));
    if (result == 5) {
        return APP_FORMS_RESULT_INVALID;
    }
    if (result == 2) {
        return APP_FORMS_RESULT_UNSUPPORTED;
    }
    if (result != 1 || info.dialog_id_bytes < 0 ||
            info.return_value_bytes < 0 || info.dialog_id_bytes >=
            (int) sizeof(request->dialog_id) || info.return_value_bytes >=
            (int) sizeof(request->return_value)) {
        return result == 0 ? APP_FORMS_RESULT_NONE : APP_FORMS_RESULT_FAILED;
    }
    result = app_forms_dialog_submission(data, document, &info,
            request->dialog_id, sizeof(request->dialog_id),
            request->return_value, sizeof(request->return_value));
    if (result == 5) {
        return APP_FORMS_RESULT_INVALID;
    }
    if (result != 1 || request->dialog_id[0] == '\0') {
        return APP_FORMS_RESULT_FAILED;
    }
    request->method = PCORE_FORM_METHOD_DIALOG;
    request->valid = 1;
    (void) document_url;
    (void) adapter;
    return APP_FORMS_RESULT_OK;
}

static int app_forms_build_multipart(const AppFormsSubmissionData *data,
        HANDLE document, const char *document_url,
        const AppFormsAdapter *adapter, AppFormRequest *request)
{
    PCoreMultipartSubmissionInfo submission_info;
    PCoreMultipartEncodeInfo encode_info;
    HANDLE submission;
    char action[APP_FORMS_URL_MAX];
    int result;

    if (adapter == NULL || adapter->resolve_url == NULL ||
            adapter->read_file == NULL || adapter->free_file == NULL) {
        return APP_FORMS_RESULT_UNSUPPORTED;
    }
    submission = app_forms_multipart_submission(data, document);
    if (submission == NULL) {
        return APP_FORMS_RESULT_FAILED;
    }
    memset(&submission_info, 0, sizeof(submission_info));
    action[0] = '\0';
    result = PCore_MultipartSubmissionInfo(submission, &submission_info,
            action, sizeof(action));
    if (result != 1 || submission_info.action_bytes < 0 ||
            submission_info.action_bytes >= (int) sizeof(action)) {
        PCore_FreeMultipartSubmission(submission);
        return APP_FORMS_RESULT_FAILED;
    }
    memset(&encode_info, 0, sizeof(encode_info));
    result = PCore_MultipartSubmissionEncode(submission,
            adapter->read_file, adapter->free_file, adapter->file_pw,
            &encode_info, NULL, 0, NULL, 0);
    if (result != 2 || encode_info.body_bytes < 0 ||
            encode_info.body_bytes > APP_FORMS_BODY_MAX_BYTES ||
            encode_info.content_type_bytes < 0 ||
            encode_info.content_type_bytes >=
            (int) sizeof(request->content_type)) {
        PCore_FreeMultipartSubmission(submission);
        return APP_FORMS_RESULT_FAILED;
    }
    if (app_forms_resolve_target(document_url, action, NULL,
            PCORE_FORM_METHOD_MULTIPART, adapter, request->target_url,
            sizeof(request->target_url)) != 0) {
        PCore_FreeMultipartSubmission(submission);
        return APP_FORMS_RESULT_FAILED;
    }
    if (app_forms_copy_body(request, NULL, encode_info.body_bytes) != 0) {
        PCore_FreeMultipartSubmission(submission);
        return APP_FORMS_RESULT_FAILED;
    }
    result = PCore_MultipartSubmissionEncode(submission,
            adapter->read_file, adapter->free_file, adapter->file_pw,
            &encode_info, request->body, encode_info.body_bytes,
            request->content_type, sizeof(request->content_type));
    PCore_FreeMultipartSubmission(submission);
    if (result != 1) {
        AppForms_ClearRequest(request);
        return APP_FORMS_RESULT_FAILED;
    }
    request->method = PCORE_FORM_METHOD_MULTIPART;
    request->valid = 1;
    return APP_FORMS_RESULT_OK;
}

static int app_forms_build_submission(const AppFormsSubmissionData *data,
        HANDLE document, const char *document_url,
        const AppFormsAdapter *adapter, AppFormRequest *request)
{
    PCoreFormSubmissionInfo info;
    char action_probe[1];
    char body_probe[1];
    char action[APP_FORMS_URL_MAX];
    char *body;
    int result;

    if (data == NULL || document == NULL || document_url == NULL ||
            document_url[0] == '\0' || adapter == NULL || request == NULL) {
        return APP_FORMS_RESULT_FAILED;
    }
    memset(&info, 0, sizeof(info));
    action_probe[0] = '\0';
    body_probe[0] = '\0';
    result = app_forms_submission(data, document, &info, action_probe,
            sizeof(action_probe), body_probe, sizeof(body_probe));
    if (result == 5) {
        return APP_FORMS_RESULT_INVALID;
    }
    if (result == 3) {
        return app_forms_build_multipart(data, document, document_url,
                adapter, request);
    }
    if (result == 6) {
        return app_forms_build_dialog(data, document, document_url, adapter,
                request);
    }
    if (result != 1 && result != 4) {
        return result == 0 ? APP_FORMS_RESULT_NONE : APP_FORMS_RESULT_FAILED;
    }
    if (info.method != PCORE_FORM_METHOD_GET &&
            info.method != PCORE_FORM_METHOD_POST) {
        return APP_FORMS_RESULT_UNSUPPORTED;
    }
    if (info.action_bytes < 0 || info.action_bytes >= (int) sizeof(action) ||
            info.body_bytes < 0 || info.body_bytes > APP_FORMS_BODY_MAX_BYTES) {
        return APP_FORMS_RESULT_FAILED;
    }
    body = (char *) malloc((size_t) info.body_bytes + 1U);
    if (body == NULL) {
        return APP_FORMS_RESULT_FAILED;
    }
    action[0] = '\0';
    result = app_forms_submission(data, document, &info, action,
            sizeof(action), body, info.body_bytes + 1);
    if (result == 5) {
        free(body);
        return APP_FORMS_RESULT_INVALID;
    }
    if (result != 1 && result != 4) {
        free(body);
        return APP_FORMS_RESULT_FAILED;
    }
    if (app_forms_resolve_target(document_url, action, body, info.method,
            adapter, request->target_url, sizeof(request->target_url)) != 0) {
        free(body);
        return APP_FORMS_RESULT_FAILED;
    }
    if (info.method == PCORE_FORM_METHOD_POST &&
            app_forms_copy_body(request, body, info.body_bytes) != 0) {
        free(body);
        return APP_FORMS_RESULT_FAILED;
    }
    free(body);
    if (info.method == PCORE_FORM_METHOD_POST) {
        app_forms_copy_text(request->content_type,
                sizeof(request->content_type),
                "application/x-www-form-urlencoded");
    }
    request->method = info.method;
    request->valid = 1;
    return APP_FORMS_RESULT_OK;
}

static int app_forms_build(const AppFormsSubmissionData *data,
        HANDLE document, const char *document_url,
        const AppFormsAdapter *adapter, AppFormRequest *out_request)
{
    if (out_request == NULL) {
        return APP_FORMS_RESULT_FAILED;
    }
    AppForms_ClearRequest(out_request);
    return app_forms_build_submission(data, document, document_url, adapter,
            out_request);
}

int AppForms_BuildAt(HANDLE document, const char *document_url, int x, int y,
        const AppFormsAdapter *adapter, AppFormRequest *out_request)
{
    AppFormsSubmissionData data;

    memset(&data, 0, sizeof(data));
    data.source = APP_FORMS_SOURCE_AT;
    data.x = x;
    data.y = y;
    return app_forms_build(&data, document, document_url, adapter,
            out_request);
}

int AppForms_BuildForTextInput(HANDLE document, const char *document_url,
        unsigned int text_index, const AppFormsAdapter *adapter,
        AppFormRequest *out_request)
{
    AppFormsSubmissionData data;

    memset(&data, 0, sizeof(data));
    data.source = APP_FORMS_SOURCE_TEXT;
    data.text_index = text_index;
    return app_forms_build(&data, document, document_url, adapter,
            out_request);
}

int AppForms_BuildById(HANDLE document, const char *document_url,
        const char *form_id, const char *submitter_id, int validate,
        const AppFormsAdapter *adapter, AppFormRequest *out_request)
{
    AppFormsSubmissionData data;

    if (form_id == NULL || form_id[0] == '\0') {
        return APP_FORMS_RESULT_FAILED;
    }
    memset(&data, 0, sizeof(data));
    data.source = validate ? APP_FORMS_SOURCE_BY_ID : APP_FORMS_SOURCE_DIRECT;
    data.form_id = form_id;
    data.submitter_id = submitter_id;
    data.validate = validate ? 1 : 0;
    return app_forms_build(&data, document, document_url, adapter,
            out_request);
}
