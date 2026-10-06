/* Positron CSS Text extension. MIT license, as the containing LibCSS. */
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

css_error css__parse_overflow_wrap(css_language *c,
        const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
    int32_t orig_ctx = *ctx;
    const css_token *token;
    css_error error;
    bool match;

    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL || token->type != CSS_TOKEN_IDENT) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }
    if (lwc_string_caseless_isequal(token->idata, c->strings[INHERIT],
            &match) == lwc_error_ok && match) {
        error = css_stylesheet_style_inherit(result, CSS_PROP_OVERFLOW_WRAP);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[INITIAL],
            &match) == lwc_error_ok && match) {
        error = css_stylesheet_style_initial(result, CSS_PROP_OVERFLOW_WRAP);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[UNSET],
            &match) == lwc_error_ok && match) {
        error = css_stylesheet_style_unset(result, CSS_PROP_OVERFLOW_WRAP);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[REVERT],
            &match) == lwc_error_ok && match) {
        error = css_stylesheet_style_revert(result, CSS_PROP_OVERFLOW_WRAP);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[NORMAL],
            &match) == lwc_error_ok && match) {
        error = css__stylesheet_style_appendOPV(result,
                CSS_PROP_OVERFLOW_WRAP, 0, OVERFLOW_WRAP_NORMAL);
    } else if (lwc_string_caseless_isequal(token->idata, c->strings[BREAK_WORD],
            &match) == lwc_error_ok && match) {
        error = css__stylesheet_style_appendOPV(result,
                CSS_PROP_OVERFLOW_WRAP, 0, OVERFLOW_WRAP_BREAK_WORD);
    } else {
        error = CSS_INVALID;
    }
    if (error != CSS_OK) {
        *ctx = orig_ctx;
    }
    return error;
}
