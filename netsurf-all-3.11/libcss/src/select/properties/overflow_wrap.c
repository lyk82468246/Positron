/* Positron CSS Text extension. MIT license, as the containing LibCSS. */
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_overflow_wrap(uint32_t opv, css_style *style,
        css_select_state *state)
{
    uint8_t value = CSS_OVERFLOW_WRAP_INHERIT;
    (void) style;
    if (hasFlagValue(opv) == false) {
        value = getValue(opv) == OVERFLOW_WRAP_BREAK_WORD ?
                CSS_OVERFLOW_WRAP_BREAK_WORD : CSS_OVERFLOW_WRAP_NORMAL;
    }
    if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
            getFlagValue(opv))) {
        return set_overflow_wrap(state->computed, value);
    }
    return CSS_OK;
}

css_error css__set_overflow_wrap_from_hint(const css_hint *hint,
        css_computed_style *style)
{
    return set_overflow_wrap(style, hint->status);
}

css_error css__initial_overflow_wrap(css_select_state *state)
{
    return set_overflow_wrap(state->computed, CSS_OVERFLOW_WRAP_NORMAL);
}

css_error css__copy_overflow_wrap(const css_computed_style *from,
        css_computed_style *to)
{
    if (from == to) {
        return CSS_OK;
    }
    return set_overflow_wrap(to, get_overflow_wrap(from));
}

css_error css__compose_overflow_wrap(const css_computed_style *parent,
        const css_computed_style *child, css_computed_style *result)
{
    return css__copy_overflow_wrap(
            get_overflow_wrap(child) == CSS_OVERFLOW_WRAP_INHERIT ?
            parent : child, result);
}
