// VectorSafeAccess.h : 向量安全访问辅助类
// 用于在访问vector时进行边界检查，防止越界访问导致崩溃

#ifndef _VECTOR_SAFE_ACCESS_H_
#define _VECTOR_SAFE_ACCESS_H_

#include <vector>
#include <atlstr.h>

// 日志宏 - 在需要的地方定义 LOG_ERROR
#ifndef LOG_ERROR_SAFE_ACCESS
#define LOG_ERROR_SAFE_ACCESS(msg) \
    { \
        CString strLog; \
        strLog.Format(_T("[VectorSafeAccess] %s [%s:%d]"), msg, _T(__FILE__), __LINE__); \
        OutputDebugString(strLog); \
    }
#endif

// 安全获取vector元素引用（带范围检查）
// 返回true表示获取成功，false表示越界
template<typename T>
inline bool SafeVectorAt(const std::vector<T>& vec, int index, const T*& pOut)
{
    if (index < 0 || index >= (int)vec.size())
    {
        CString strLog;
        strLog.Format(_T("[VectorSafeAccess] 越界访问! index=%d, size=%d [%s:%d]"),
            index, (int)vec.size(), _T(__FILE__), __LINE__);
        OutputDebugString(strLog);
        pOut = nullptr;
        return false;
    }
    pOut = &vec[index];
    return true;
}

// 安全获取vector元素引用（返回指针，NULL表示越界）
template<typename T>
inline const T* SafeVectorAt(const std::vector<T>& vec, int index)
{
    if (index < 0 || index >= (int)vec.size())
    {
        CString strLog;
        strLog.Format(_T("[VectorSafeAccess] 越界访问! index=%d, size=%d [%s:%d]"),
            index, (int)vec.size(), _T(__FILE__), __LINE__);
        OutputDebugString(strLog);
        return nullptr;
    }
    return &vec[index];
}

// 安全获取vector元素引用（非const版本）
template<typename T>
inline T* SafeVectorAt(std::vector<T>& vec, int index)
{
    if (index < 0 || index >= (int)vec.size())
    {
        CString strLog;
        strLog.Format(_T("[VectorSafeAccess] 越界访问! index=%d, size=%d [%s:%d]"),
            index, (int)vec.size(), _T(__FILE__), __LINE__);
        OutputDebugString(strLog);
        return nullptr;
    }
    return &vec[index];
}

// 安全获取vector元素值（越界时返回默认值）
template<typename T>
inline T SafeVectorValue(const std::vector<T>& vec, int index, const T& defaultVal = T())
{
    if (index < 0 || index >= (int)vec.size())
    {
        CString strLog;
        strLog.Format(_T("[VectorSafeAccess] 越界访问! index=%d, size=%d [%s:%d]"),
            index, (int)vec.size(), _T(__FILE__), __LINE__);
        OutputDebugString(strLog);
        return defaultVal;
    }
    return vec[index];
}

// 安全的for循环宏 - 自动处理越界情况
// 用法: SAFE_FOR_VECTOR(vec, ii) { ... }
#define SAFE_FOR_VECTOR(vec, index) \
    for (int index = 0; index < (int)(vec).size(); index++)

// 安全访问宏 - 在访问前检查索引
// 用法: if (IS_VALID_INDEX(vec, idx)) { ... }
#define IS_VALID_INDEX(vec, idx) \
    ((idx) >= 0 && (idx) < (int)(vec).size())

// 安全索引访问宏 - 越界时返回默认值
// 用法: T value = SAFE_IDX_ACCESS(vec, idx, defaultVal);
#define SAFE_IDX_ACCESS(vec, idx, defVal) \
    (((idx) >= 0 && (idx) < (int)(vec).size()) ? (vec)[idx] : (defVal))

// 带日志的安全访问
#define SAFE_ACCESS_WITH_LOG(vec, idx, logMsg) \
    (IS_VALID_INDEX(vec, idx) ? vec[idx] : ([&]()->auto& { \
        CString strLog; \
        strLog.Format(_T("[VectorSafeAccess] %s 越界! idx=%d, size=%d [%s:%d]"), \
            logMsg, idx, (int)vec.size(), _T(__FILE__), __LINE__); \
        OutputDebugString(strLog); \
        static auto _dummy = decltype(vec)::value_type(); \
        return _dummy; \
    }()))

#endif // _VECTOR_SAFE_ACCESS_H_
