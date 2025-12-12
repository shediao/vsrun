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

#if defined(__MINGW32__) || defined(__MINGW64__)
__CRT_UUID_DECL(ISetupConfiguration, 0x42843719, 0xDB4C, 0x46C2, 0x8E, 0x7C,
                0x64, 0xF1, 0x81, 0x6E, 0xFD, 0x5B);
__CRT_UUID_DECL(ISetupConfiguration2, 0x26AAB78C, 0x4A60, 0x49D6, 0xAF, 0x3B,
                0x3C, 0x35, 0xBC, 0x93, 0x36, 0x5D);
__CRT_UUID_DECL(ISetupPackageReference, 0xda8d8a16, 0xb2b6, 0x4487, 0xa2, 0xf1,
                0x59, 0x4c, 0xcc, 0xcd, 0x6b, 0xf5);
__CRT_UUID_DECL(ISetupHelper, 0x42b21b78, 0x6192, 0x463e, 0x87, 0xbf, 0xd5,
                0x77, 0x83, 0x8f, 0x1d, 0x5c);
__CRT_UUID_DECL(IEnumSetupInstances, 0x6380BCFF, 0x41D3, 0x4B2E, 0x8B, 0x2E,
                0xBF, 0x8A, 0x68, 0x10, 0xC8, 0x48);
__CRT_UUID_DECL(ISetupInstance2, 0x89143C9A, 0x05AF, 0x49B0, 0xB7, 0x17, 0x72,
                0xE2, 0x18, 0xA2, 0x18, 0x5C);
__CRT_UUID_DECL(ISetupInstance, 0xB41463C3, 0x8866, 0x43B5, 0xBC, 0x33, 0x2B,
                0x06, 0x76, 0xF7, 0xF4, 0x2E);
__CRT_UUID_DECL(ISetupInstanceCatalog, 0x9AD8E40F, 0x39A2, 0x40F1, 0xBF, 0x64,
                0x0A, 0x6C, 0x50, 0xDD, 0x9E, 0xEB);
__CRT_UUID_DECL(SetupConfiguration, 0x177F0C4A, 0x1CD3, 0x4DE7, 0xA3, 0x2C,
                0x71, 0xDB, 0xBB, 0x9F, 0xA3, 0x6D);
#endif  // defined(__MINGW32__) || defined(__MINGW64__)

_COM_SMARTPTR_TYPEDEF(ISetupInstance, __uuidof(ISetupInstance));
_COM_SMARTPTR_TYPEDEF(ISetupInstance2, __uuidof(ISetupInstance2));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(ISetupPackageReference, __uuidof(ISetupPackageReference));
_COM_SMARTPTR_TYPEDEF(ISetupInstanceCatalog, __uuidof(ISetupInstanceCatalog));

#include <algorithm>
#include <cstdint>  // uint64_t
#include <map>
#include <stdexcept>  // std::runtime_error
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

class win32_exception : public std::runtime_error {
 public:
  win32_exception(_In_ DWORD code, _In_z_ const char* what) noexcept
      : std::runtime_error(what), m_code(code) {}

  win32_exception(_In_ const win32_exception& obj) noexcept
      : std::runtime_error(obj) {
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
    std::map<std::string, std::string> const& sort_by = {},
    int debug_level = 0);

std::wstring to_wstring(const std::string_view str,
                        const UINT from_codepage = CP_UTF8);
std::string to_string(const std::wstring_view wstr,
                      const UINT to_codepage = CP_UTF8);

std::pair<bool, std::string> check_product_id(const std::string& val);
std::pair<bool, std::string> check_sort_by(std::string const& sort_by);
#endif  // VISUAL_STUDIO_H_
