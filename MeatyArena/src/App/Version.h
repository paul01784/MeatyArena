#pragma once

#define MEATYARENA_VERSION_LITERAL "1.0.3"
#define MEATYARENA_WIDEN_IMPL(value) L##value
#define MEATYARENA_WIDEN(value) MEATYARENA_WIDEN_IMPL(value)

namespace AppVersion
{
    inline constexpr char Number[] = MEATYARENA_VERSION_LITERAL;
    inline constexpr wchar_t WindowTitle[] = L"MeatyArena " MEATYARENA_WIDEN(MEATYARENA_VERSION_LITERAL);
} // namespace AppVersion

#undef MEATYARENA_WIDEN
#undef MEATYARENA_WIDEN_IMPL
#undef MEATYARENA_VERSION_LITERAL
