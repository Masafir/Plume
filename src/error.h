#pragma once
#include <windows.h>
struct Failure {
    HRESULT hr;
    const wchar_t* operation;
};
inline void Check(HRESULT hr, const wchar_t* operation) {
    if (FAILED(hr))
        throw Failure{hr, operation};
}
