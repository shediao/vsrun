#ifndef VISUAL_STUDIO_H_
#define VISUAL_STUDIO_H_
#include <comdef.h>
#include <comutil.h>
#include <fileapi.h>
#include <minwinbase.h>
#include <oaidl.h>
#include <windows.h>
#include <winerror.h>

#include "Setup.Configuration.h"

_COM_SMARTPTR_TYPEDEF(ISetupInstance, __uuidof(ISetupInstance));
_COM_SMARTPTR_TYPEDEF(ISetupInstance2, __uuidof(ISetupInstance2));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(ISetupPackageReference, __uuidof(ISetupPackageReference));
_COM_SMARTPTR_TYPEDEF(ISetupInstanceCatalog, __uuidof(ISetupInstanceCatalog));

#include <algorithm>
#include <map>
#include <string>
#include <vector>

template <typename T, typename = std::void_t<>>
struct has_push_back : public std::false_type {};
template <typename C>
struct has_push_back<C, std::void_t<decltype(std::declval<C>().push_back(
                            std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_push_back_v = has_push_back<T>::value;

template <typename T, typename = std::void_t<>>
struct has_emplace_back : public std::false_type {};
template <typename C>
struct has_emplace_back<C, std::void_t<decltype(std::declval<C>().emplace_back(
                               std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_emplace_back_v = has_emplace_back<T>::value;

template <typename T, typename = std::void_t<>>
struct has_insert : public std::false_type {};
template <typename C>
struct has_insert<C, std::void_t<decltype(std::declval<C>().insert(
                         std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_insert_v = has_insert<T>::value;

template <typename T, typename = std::void_t<>>
struct has_emplace : public std::false_type {};
template <typename C>
struct has_emplace<C, std::void_t<decltype(std::declval<C>().emplace(
                          std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_emplace_v = has_emplace<T>::value;

template <typename T, typename = std::void_t<>>
struct has_push : public std::false_type {};
template <typename C>
struct has_push<C, std::void_t<decltype(std::declval<C>().push(
                       std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_push_v = has_push<T>::value;

template <typename T, typename = std::void_t<>>
struct has_push_front : public std::false_type {};
template <typename C>
struct has_push_front<C, std::void_t<decltype(std::declval<C>().push_front(
                             std::declval<typename C::value_type>()))>>
    : public std::true_type {};
template <typename T>
constexpr bool has_push_front_v = has_push_front<T>::value;
template <typename CharT, typename F, typename C>
  requires std::is_same_v<bool,
                          decltype(std::declval<F>()(std::declval<CharT>()))>
C& split_to_if(C& to, const std::basic_string<CharT>& str, F f,
               int max_count = -1, bool is_compress_token = false) {
  auto begin = str.begin();
  auto delimiter = begin;
  int count = 0;

  while ((max_count < 0 || count++ < max_count) &&
         (delimiter = std::find_if(begin, str.end(), f)) != str.end()) {
    to.insert(to.end(), {begin, delimiter});
    if (is_compress_token) {
      begin = std::find_if_not(delimiter, str.end(), f);
    } else {
      begin = std::next(delimiter);
    }
  }

  if constexpr (has_emplace_back_v<C>) {
    to.emplace_back(begin, str.end());
  } else if constexpr (has_emplace_v<C>) {
    to.emplace(begin, str.end());
  } else if constexpr (has_push_back_v<C>) {
    to.push_back({begin, str.end()});
  } else if constexpr (has_insert_v<C>) {
    to.insert({begin, str.end()});
  } else if constexpr (has_push_v<C>) {
    to.push({begin, str.end()});
  } else if constexpr (has_push_front_v<C>) {
    to.push_front({begin, str.end()});
  } else {
    static_assert(
        !std::is_same_v<C, C>,
        "The container does not support adding elements via a known method.");
  }

  return to;
}

inline std::vector<std::string> split(std::string const& s, char delim,
                                      int max) {
  std::vector<std::string> result;
  split_to_if(result, s, [delim](char c) { return c == delim; }, max, false);
  return result;
}

struct VisualStudio {
  uint64_t version_;
  FILETIME install_datetime_;
  std::wstring install_version_;
  std::wstring install_path_;
  std::wstring display_name_;
  std::wstring product_id_;
  bool is_complete_;
  bool is_prerelease_;
  std::vector<std::wstring> workloads_;
  bool is_complete() const { return is_complete_; }
  bool is_prerelease() const { return is_prerelease_; }
  bool is_product_match(std::wstring const& product_pattern) const;
  bool is_workload_match(std::wstring const& workload_pattern) const;
  bool is_version_match(uint64_t min, uint64_t max) const;
};

class win32_exception : public std::exception {
 public:
  win32_exception(_In_ DWORD code, _In_z_ const char* what) noexcept
      : std::exception(what), m_code(code) {}

  win32_exception(_In_ const win32_exception& obj) noexcept
      : std::exception(obj) {
    m_code = obj.m_code;
  }

  DWORD code() const noexcept { return m_code; }

 private:
  DWORD m_code;
};

class CoInitializer {
 public:
  CoInitializer() {
    hr = ::CoInitialize(NULL);
    if (FAILED(hr)) {
      throw win32_exception(hr, "failed to initialize COM");
    }
  }

  ~CoInitializer() {
    if (SUCCEEDED(hr)) {
      ::CoUninitialize();
    }
  }

 private:
  HRESULT hr;
};

std::wostream& operator<<(std::wostream& out, VisualStudio const& vs);

std::vector<VisualStudio> GetMatchedVisualStudios(
    ISetupConfiguration2Ptr& config, std::string const& version,
    std::string const& product = "*", std::string const& workload = "*",
    std::map<std::string, std::string> const& sort_by = {});

std::wstring to_wstring(const std::string_view str,
                        const UINT from_codepage = CP_UTF8);
std::string to_string(const std::wstring_view wstr,
                      const UINT to_codepage = CP_UTF8);

std::pair<bool, std::string> check_sort_by(std::string const& sort_by);
#endif  // VISUAL_STUDIO_H_
