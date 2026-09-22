#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "app_i18n.h"
#include "resource.h"

#define APP_I18N_PAGE_MAX 32768UL

typedef LANGID (WINAPI *AppGetLangProc)(void);

typedef struct AppTextResourcePair {
    UINT english_id;
    UINT chinese_id;
} AppTextResourcePair;

static HINSTANCE g_i18n_instance = NULL;
static AppLanguage g_i18n_language = APP_LANGUAGE_EN_US;

static const AppTextResourcePair g_text_resources[APP_TEXT_COUNT] = {
    { IDS_APP_BACK_EN, IDS_APP_BACK_ZH },
    { IDS_APP_MENU_EN, IDS_APP_MENU_ZH },
    { IDS_APP_FORWARD_EN, IDS_APP_FORWARD_ZH },
    { IDS_APP_HOME_EN, IDS_APP_HOME_ZH },
    { IDS_APP_ADDRESS_EN, IDS_APP_ADDRESS_ZH },
    { IDS_APP_REFRESH_EN, IDS_APP_REFRESH_ZH },
    { IDS_APP_EXIT_EN, IDS_APP_EXIT_ZH },
    { IDS_STATUS_OFFLINE_EN, IDS_STATUS_OFFLINE_ZH },
    { IDS_STATUS_HISTORY_EN, IDS_STATUS_HISTORY_ZH },
    { IDS_STATUS_READY_WELCOME_EN, IDS_STATUS_READY_WELCOME_ZH },
    { IDS_STATUS_READY_CONTROLS_EN, IDS_STATUS_READY_CONTROLS_ZH },
    { IDS_STATUS_ADDRESS_INVALID_EN, IDS_STATUS_ADDRESS_INVALID_ZH },
    { IDS_STATUS_NO_BACK_EN, IDS_STATUS_NO_BACK_ZH },
    { IDS_STATUS_NO_FORWARD_EN, IDS_STATUS_NO_FORWARD_ZH },
    { IDS_STATUS_LAYOUT_EN, IDS_STATUS_LAYOUT_ZH },
    { IDS_STATUS_ADDRESS_CANCELLED_EN, IDS_STATUS_ADDRESS_CANCELLED_ZH },
    { IDS_STATUS_LOADING_EN, IDS_STATUS_LOADING_ZH },
    { IDS_STATUS_READY_REMOTE_EN, IDS_STATUS_READY_REMOTE_ZH },
    { IDS_STATUS_NETWORK_EN, IDS_STATUS_NETWORK_ZH },
    { IDS_ERROR_CORE_INIT_EN, IDS_ERROR_CORE_INIT_ZH },
    { IDS_ERROR_HISTORY_INIT_EN, IDS_ERROR_HISTORY_INIT_ZH },
    { IDS_ERROR_CLASS_REGISTER_EN, IDS_ERROR_CLASS_REGISTER_ZH },
    { IDS_ERROR_WINDOW_CREATE_EN, IDS_ERROR_WINDOW_CREATE_ZH }
};

static LANGID app_call_language_proc(HMODULE module, LPCTSTR name)
{
    AppGetLangProc proc;

    if (module == NULL || name == NULL) {
        return 0;
    }
    proc = (AppGetLangProc) GetProcAddress(module, name);
    return (proc == NULL) ? 0 : proc();
}

static LANGID app_detect_language(void)
{
    HMODULE core;
    LANGID language;

    core = GetModuleHandle(TEXT("coredll.dll"));
    language = app_call_language_proc(core,
            TEXT("GetUserDefaultUILanguage"));
    if (language != 0) {
        return language;
    }
    language = app_call_language_proc(core,
            TEXT("GetSystemDefaultUILanguage"));
    if (language != 0) {
        return language;
    }
    language = app_call_language_proc(core, TEXT("GetUserDefaultLangID"));
    if (language != 0) {
        return language;
    }
    return app_call_language_proc(core, TEXT("GetSystemDefaultLangID"));
}

static AppLanguage app_language_from_langid(LANGID language)
{
    WORD primary;
    WORD sublanguage;

    primary = PRIMARYLANGID(language);
    sublanguage = SUBLANGID(language);
    if (primary == LANG_CHINESE &&
            (sublanguage == SUBLANG_CHINESE_SIMPLIFIED ||
             sublanguage == SUBLANG_CHINESE_SINGAPORE)) {
        return APP_LANGUAGE_ZH_CN;
    }
    return APP_LANGUAGE_EN_US;
}

static int app_resource_exists(UINT resource_id, LPCTSTR resource_type)
{
    if (g_i18n_instance == NULL || resource_id == 0) {
        return 0;
    }
    return FindResource(g_i18n_instance, MAKEINTRESOURCE(resource_id),
            resource_type) != NULL;
}

static UINT app_select_rcdata(UINT english_id, UINT chinese_id)
{
    if (g_i18n_language == APP_LANGUAGE_ZH_CN &&
            app_resource_exists(chinese_id, RT_RCDATA)) {
        return chinese_id;
    }
    if (app_resource_exists(english_id, RT_RCDATA)) {
        return english_id;
    }
    return 0;
}

int AppI18n_Init(HINSTANCE instance)
{
    LANGID language;

    if (instance == NULL) {
        return 1;
    }
    g_i18n_instance = instance;
    language = app_detect_language();
    g_i18n_language = app_language_from_langid(language);
    return 0;
}

AppLanguage AppI18n_CurrentLanguage(void)
{
    return g_i18n_language;
}

UINT AppI18n_MenuBarResource(void)
{
    return app_select_rcdata(IDR_APP_MENUBAR_EN, IDR_APP_MENUBAR_ZH);
}

static int app_load_string_resource(UINT resource_id, WCHAR *buffer,
        int capacity)
{
    int length;

    if (g_i18n_instance == NULL || resource_id == 0 || buffer == NULL ||
            capacity <= 1) {
        return 0;
    }
    buffer[0] = L'\0';
    length = LoadStringW(g_i18n_instance, resource_id, buffer, capacity);
    if (length <= 0 || length >= capacity) {
        buffer[capacity - 1] = L'\0';
        return 0;
    }
    buffer[length] = L'\0';
    return length;
}

int AppI18n_LoadString(AppTextId text_id, WCHAR *buffer, int capacity)
{
    const AppTextResourcePair *resources;
    UINT resource_id;
    int length;

    if (text_id < 0 || text_id >= APP_TEXT_COUNT || buffer == NULL ||
            capacity <= 1) {
        return 0;
    }
    resources = &g_text_resources[(int) text_id];
    resource_id = (g_i18n_language == APP_LANGUAGE_ZH_CN) ?
            resources->chinese_id : resources->english_id;
    length = app_load_string_resource(resource_id, buffer, capacity);
    if (length == 0 && resource_id != resources->english_id) {
        length = app_load_string_resource(resources->english_id, buffer,
                capacity);
    }
    return length;
}

static UINT app_page_resource(int page_kind, AppLanguage language)
{
    UINT english_id;
    UINT chinese_id;

    english_id = 0;
    chinese_id = 0;
    if (page_kind == APP_I18N_PAGE_WELCOME) {
        english_id = IDR_APP_WELCOME_EN;
        chinese_id = IDR_APP_WELCOME_ZH;
    } else if (page_kind == APP_I18N_PAGE_CONTROLS) {
        english_id = IDR_APP_CONTROLS_EN;
        chinese_id = IDR_APP_CONTROLS_ZH;
    }
    if (english_id == 0 || chinese_id == 0) {
        return 0;
    }
    if (language == APP_LANGUAGE_ZH_CN &&
            app_resource_exists(chinese_id, RT_RCDATA)) {
        return chinese_id;
    }
    if (app_resource_exists(english_id, RT_RCDATA)) {
        return english_id;
    }
    return 0;
}

int AppI18n_LoadPage(int page_kind, char **out_bytes,
        unsigned int *out_length)
{
    HRSRC resource;
    HGLOBAL loaded;
    const void *source;
    DWORD size;
    UINT resource_id;
    char *bytes;

    if (out_bytes == NULL || out_length == NULL ||
            g_i18n_instance == NULL) {
        return 1;
    }
    *out_bytes = NULL;
    *out_length = 0;
    resource_id = app_page_resource(page_kind, g_i18n_language);
    if (resource_id == 0) {
        return 1;
    }
    resource = FindResource(g_i18n_instance, MAKEINTRESOURCE(resource_id),
            RT_RCDATA);
    if (resource == NULL) {
        return 1;
    }
    size = SizeofResource(g_i18n_instance, resource);
    if (size == 0 || size > APP_I18N_PAGE_MAX) {
        return 1;
    }
    loaded = LoadResource(g_i18n_instance, resource);
    source = (loaded == NULL) ? NULL : LockResource(loaded);
    if (source == NULL) {
        return 1;
    }
    bytes = (char *) malloc((size_t) size + 1U);
    if (bytes == NULL) {
        return 1;
    }
    memcpy(bytes, source, (size_t) size);
    bytes[size] = '\0';
    *out_bytes = bytes;
    *out_length = (unsigned int) size;
    return 0;
}

void AppI18n_FreePage(char *bytes)
{
    if (bytes != NULL) {
        free(bytes);
    }
}
