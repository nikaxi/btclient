#ifndef INC_BENCODE_HPP
#define INC_BENCODE_HPP

// ===== 标准库头文件 =====
#include <algorithm>    // std::copy, std::advance
#include <cassert>      // assert 断言
#include <cctype>       // std::isdigit
#include <charconv>     // std::to_chars (C++17 高性能整数转字符串)
#include <cstddef>      // std::size_t, std::ptrdiff_t
#include <cstring>      // std::strlen
#include <iostream>     // std::ostream
#include <iterator>     // std::begin/end, ostreambuf_iterator
#include <limits>       // std::numeric_limits (整数范围)
#include <map>          // std::map (dict 底层容器)
#include <memory>       // std::unique_ptr (map_proxy 使用)
#include <ranges>       // C++20 ranges/view 概念
#include <span>         // C++20 std::span
#include <sstream>      // std::stringstream (encode 到 string 时使用)
#include <stack>        // std::stack (解码器的显式栈)
#include <stdexcept>    // 异常基类
#include <string>       // std::string
#include <string_view>  // std::string_view (零拷贝视图)
#include <variant>      // std::variant (类型安全的联合体)
#include <vector>       // std::vector (list 底层容器)

// ===== 可选 Boost 支持 =====
// 如果环境中有 boost::variant，则额外提供基于 boost 的类型别名
#if __has_include(<boost/variant.hpp>)
#  include <boost/variant.hpp>
#  define BENCODE_HAS_BOOST
#endif

namespace bencode {

  // ========================================================================
  // 概念(Concepts)与类型特征 - 用于约束模板参数
  // ========================================================================
  namespace detail {

    // 可迭代概念: 类型必须有 begin()/end()
    template<typename T>
    concept iterable = requires(T &t) {
      std::begin(t);
      std::end(t);
    };

    // 类字符串概念: 可迭代 + 有 size() + 元素类型为 char
    template<typename T>
    concept stringish = iterable<T> && requires(T &t) {
      std::size(t);
      requires std::same_as<std::iter_value_t<decltype(std::begin(t))>, char>;
    };

    // 映射概念: 可迭代 + 有 key_type/mapped_type + key 是类字符串
    template<typename T>
    concept mapping = iterable<T> && requires {
      typename T::key_type;
      typename T::mapped_type;
      requires stringish<typename T::key_type>;
    };

  } // namespace detail

  // Variant 操作的特化点(自定义点对象)，允许同时支持 std::variant 和 boost::variant
  template<template<typename ...> typename T>
  struct variant_traits;

  // ===== 宏: 生成 map_proxy 的单参数转发函数 =====
  // specs 通常是 "const" 或空
#define BENCODE_MAP_PROXY_FN_1(name, specs)                                   \
  template<typename T>                                                        \
  decltype(auto) name(T &&t) specs {                                          \
    return proxy_->name(std::forward<T>(t));                                  \
  }

  // ===== 宏: 生成 map_proxy 的多参数转发函数 =====
#define BENCODE_MAP_PROXY_FN_N(name, specs)                                   \
  template<typename ...T>                                                     \
  decltype(auto) name(T &&...t) specs {                                       \
    return proxy_->name(std::forward<T>(t)...);                               \
  }

  // ========================================================================
  // map_proxy: std::map 的间接包装器
  // 目的: 解决 std::map 不支持不完整类型(incomplete type)的问题
  // 因为 basic_data 在定义时引用自身(递归类型)，直接用作 map 的 value_type
  // 在某些编译器上会失败。用 unique_ptr 包一层就解决了。
  // ========================================================================
  template<typename Key, typename Value>
  class map_proxy {
  public:
    using map_type = std::map<Key, Value>;
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;

    // --- 构造/赋值 ---
    map_proxy() : proxy_(new map_type()) {}
    map_proxy(const map_proxy &rhs) : proxy_(new map_type(*rhs.proxy_)) {}
    map_proxy(map_proxy &&rhs) noexcept : proxy_(std::move(rhs.proxy_)) {}
    map_proxy(std::initializer_list<value_type> i) : proxy_(new map_type(i)) {}

    map_proxy operator =(const map_proxy &rhs) {
      *proxy_ = *rhs.proxy_;
      return *this;
    }

    map_proxy operator =(map_proxy &&rhs) {
      *proxy_ = std::move(*rhs.proxy_);
      return *this;
    }

    void swap(map_proxy &rhs) { proxy_->swap(*rhs.proxy_); }

    // 隐式转换为 std::map 引用，方便与期望 map 的 API 交互
    operator map_type &() { return *proxy_; };
    operator const map_type &() const { return *proxy_; };

    // 指针访问运算符
    map_type & operator *() { return *proxy_; }
    const map_type & operator *() const { return *proxy_; }
    map_type * operator ->() { return proxy_.get(); }
    const map_type * operator ->() const { return proxy_.get(); }

    // --- 元素访问 ---
    template<typename K>
    mapped_type & at(K &&k) { return proxy_->at(std::forward<K>(k)); }
    template<typename K>
    const mapped_type &
    at(K &&k) const { return proxy_->at(std::forward<K>(k)); }
    template<typename K>
    mapped_type & operator [](K &&k) { return (*proxy_)[std::forward<K>(k)]; }

    // --- 迭代器 (全部转发给内部 map) ---
    auto begin() noexcept { return proxy_->begin(); }
    auto begin() const noexcept { return proxy_->begin(); }
    auto cbegin() const noexcept { return proxy_->cbegin(); }
    auto end() noexcept { return proxy_->end(); }
    auto end() const noexcept { return proxy_->end(); }
    auto cend() const noexcept { return proxy_->cend(); }
    auto rbegin() noexcept { return proxy_->rbegin(); }
    auto rbegin() const noexcept { return proxy_->rbegin(); }
    auto crbegin() const noexcept { return proxy_->crbegin(); }
    auto rend() noexcept { return proxy_->rend(); }
    auto rend() const noexcept { return proxy_->rend(); }
    auto crend() const noexcept { return proxy_->crend(); }

    // --- 容量 ---
    bool empty() const noexcept { return proxy_->empty(); }
    auto size() const noexcept { return proxy_->size(); }
    auto max_size() const noexcept { return proxy_->max_size(); }

    // --- 修改操作 (通过宏批量生成转发) ---
    void clear() noexcept { proxy_->clear(); }
    BENCODE_MAP_PROXY_FN_N(insert,)           // insert(...)
    BENCODE_MAP_PROXY_FN_N(insert_or_assign,) // insert_or_assign(...)
    BENCODE_MAP_PROXY_FN_N(emplace,)          // emplace(...)
    BENCODE_MAP_PROXY_FN_N(emplace_hint,)     // emplace_hint(...)
    BENCODE_MAP_PROXY_FN_N(try_emplace,)      // try_emplace(...)
    BENCODE_MAP_PROXY_FN_N(erase,)            // erase(...)

    // --- 查找操作 ---
    BENCODE_MAP_PROXY_FN_1(count, const)
    BENCODE_MAP_PROXY_FN_1(find,)
    BENCODE_MAP_PROXY_FN_1(find, const)
    BENCODE_MAP_PROXY_FN_1(equal_range,)
    BENCODE_MAP_PROXY_FN_1(equal_range, const)
    BENCODE_MAP_PROXY_FN_1(lower_bound,)
    BENCODE_MAP_PROXY_FN_1(lower_bound, const)
    BENCODE_MAP_PROXY_FN_1(upper_bound,)
    BENCODE_MAP_PROXY_FN_1(upper_bound, const)

    auto key_comp() const { return proxy_->key_comp(); }
    auto value_comp() const { return proxy_->value_comp(); }

    // 比较运算符
    friend bool operator ==(const map_proxy &lhs, const map_proxy &rhs) {
      return *lhs == *rhs;
    }
    friend auto operator <=>(const map_proxy &lhs, const map_proxy &rhs) {
      return *lhs <=> *rhs;
    }
  private:
    std::unique_ptr<map_type> proxy_; // 核心: 用指针绕过不完整类型限制
  };

  // ===== 宏: 为 basic_data 生成带完美转发的访问器 =====
  // 生成 &, &&, const &, const && 四个重载，确保值类别正确传播
#define BENCODE_DATA_GETTER(func, impl, arg_type, container_type)             \
  basic_data & func(const arg_type &key) & {                                  \
    return impl<container_type>(*this, key);                                  \
  }                                                                           \
  basic_data && func(const arg_type &key) && {                                \
    return std::move(impl<container_type>(std::move(*this), key));            \
  }                                                                           \
  const basic_data & func(const arg_type &key) const & {                      \
    return impl<container_type>(*this, key);                                  \
  }                                                                           \
  const basic_data && func(const arg_type &key) const && {                    \
    return std::move(impl<container_type>(std::move(*this), key));            \
  }

  // ========================================================================
  // basic_data: Bencode 数据的通用表示
  // 模板参数:
  //   Variant - variant 类型 (std::variant 或 boost::variant)
  //   I       - 整数类型 (通常 long long)
  //   S       - 字符串类型 (std::string 或 std::string_view)
  //   L       - 列表模板 (通常 std::vector)
  //   D       - 字典模板 (通常 map_proxy)
  // ========================================================================
  template<template<typename ...> typename Variant, typename I, typename S,
           template<typename ...> typename L, template<typename ...> typename D>
  class basic_data : public Variant<I, S, L<basic_data<Variant, I, S, L, D>>,
                                    D<S, basic_data<Variant, I, S, L, D>>> {
  public:
    // 导出各组件类型
    using integer = I;
    using string = S;
    using list = L<basic_data>;   // 注意: 这里 basic_data 作为模板参数传入自身 → 递归类型
    using dict = D<S, basic_data>;

    using base_type = Variant<integer, string, list, dict>;
    using base_type::base_type; // 继承 variant 的所有构造函数

    // 获取底层 variant 的引用(四种值类别)
    base_type & base() & { return *this; }
    base_type && base() && { return std::move(*this); }
    const base_type & base() const & { return *this; }
    const base_type && base() const && { return std::move(*this); }

    // 通过宏生成 at() 和 operator[] 的四重载版本
    // at(integer) → list 访问; at(string) → dict 访问
    BENCODE_DATA_GETTER(at,          at_impl,    integer, list)
    BENCODE_DATA_GETTER(at,          at_impl,    string,  dict)
    BENCODE_DATA_GETTER(operator [], index_impl, integer, list)
    BENCODE_DATA_GETTER(operator [], index_impl, string,  dict)

  private:
    // at_impl: 从 variant 中提取指定容器类型，然后调用 .at(key)
    template<typename Type, typename Self, typename Key>
    static inline decltype(auto) at_impl(Self &&self, Key &&key) {
      return variant_traits<Variant>::template get<Type>(
        std::forward<Self>(self)
      ).at(std::forward<Key>(key));
    }

    // index_impl: 同上，但调用 operator[]
    template<typename Type, typename Self, typename Key>
    static inline decltype(auto) index_impl(Self &&self, Key &&key) {
      return variant_traits<Variant>::template get<Type>(
        std::forward<Self>(self)
      )[std::forward<Key>(key)];
    }
  };

  // variant_traits_for: 从 basic_data 实例反推其使用的 Variant 类型
  template<typename T>
  struct variant_traits_for;

  template<template<typename ...> typename Variant,
           typename I, typename S, template<typename ...> typename L,
           template<typename ...> typename D>
  struct variant_traits_for<basic_data<Variant, I, S, L, D>>
    : variant_traits<Variant> {};

  // ===== std::variant 的 traits 特化 =====
  template<>
  struct variant_traits<std::variant> {
    // visit: 转发到 std::visit，但先解包 .base()
    template<typename Visitor, typename ...Variants>
    inline static decltype(auto)
    visit(Visitor &&visitor, Variants &&...variants) {
      return std::visit(std::forward<Visitor>(visitor),
                        std::forward<Variants>(variants).base()...);
    }

    // get: 按类型提取
    template<typename Type, typename Variant>
    inline static decltype(auto) get(Variant &&variant) {
      return std::get<Type>(std::forward<Variant>(variant).base());
    }

    // get_if: 安全提取指针
    template<typename Type, typename Variant>
    inline static decltype(auto) get_if(Variant *variant) {
      return std::get_if<Type>(&variant->base());
    }

    // index: 获取当前持有的类型索引
    template<typename Variant>
    inline static auto index(const Variant &variant) {
      return variant.index();
    }
  };

  // ===== 预定义类型别名 =====
  // data: 拥有所有权的完整数据类型
  using data = basic_data<std::variant, long long, std::string, std::vector,
                          map_proxy>;
  // data_view: 零拷贝视图类型(string 替换为 string_view)
  using data_view = basic_data<std::variant, long long, std::string_view,
                               std::vector, map_proxy>;

#ifdef BENCODE_HAS_BOOST
  // ===== boost::variant 的 traits 特化 =====
  template<>
  struct variant_traits<boost::variant> {
    template<typename Visitor, typename ...Variants>
    static decltype(auto)
    visit(Visitor &&visitor, Variants &&...variants) {
      return boost::apply_visitor(std::forward<Visitor>(visitor),
                                  std::forward<Variants>(variants).base()...);
    }

    template<typename Type, typename Variant>
    inline static decltype(auto) get(Variant &&variant) {
      return boost::get<Type>(std::forward<Variant>(variant));
    }

    template<typename Type, typename Variant>
    inline static decltype(auto) get_if(Variant *variant) {
      return boost::get<Type>(variant);
    }

    template<typename Variant>
    inline static auto index(const Variant &variant) {
      return variant.which();
    }
  };

  using boost_data = basic_data<boost::variant, long long, std::string,
                                std::vector, map_proxy>;
  using boost_data_view = basic_data<boost::variant, long long,
                                     std::string_view, std::vector, map_proxy>;
#endif

  // 便捷类型别名
  using integer = data::integer;
  using string = data::string;
  using list = data::list;
  using dict = data::dict;

  using integer_view = data_view::integer;
  using string_view = data_view::string;
  using list_view = data_view::list;
  using dict_view = data_view::dict;

  // EOF 行为枚举: 解码流时是否检查到达文件末尾
  enum eof_behavior {
    check_eof,    // 到达 EOF 时设置流的 eofbit
    no_check_eof  // 不设置
  };

  // ===== 异常类型 =====
  struct syntax_error : std::runtime_error {
    using std::runtime_error::runtime_error;
  };

  struct end_of_input_error : syntax_error {
    end_of_input_error() : syntax_error("unexpected end of input") {}
  };

  // 解码错误: 携带偏移量和嵌套异常
  class decode_error : public std::runtime_error {
  public:
    decode_error(std::string message, std::size_t offset,
                 std::exception_ptr e = {})
      : runtime_error(message + ", at offset " + std::to_string(offset)),
        offset_(offset), nested_(e) {}

    [[noreturn]] void rethrow_nested() const {
      if(nested_)
        std::rethrow_exception(nested_);
      std::terminate();
    }

    std::exception_ptr nested_ptr() const noexcept {
      return nested_;
    }

    std::size_t offset() const noexcept {
      return offset_;
    }
  private:
    std::size_t offset_;          // 出错时在输入中的字节偏移
    std::exception_ptr nested_;   // 原始异常(保留完整类型信息)
  };

  // ========================================================================
  // 解码实现细节
  // ========================================================================
  namespace detail {

    // 检查整数乘法是否溢出 (value * 10 + digit > max)
    template<std::integral Integer>
    inline void check_overflow(Integer value, Integer digit) {
      using limits = std::numeric_limits<Integer>;
      // 括号包裹 max/min 是为了防止 windows.h 的宏污染
      if((value > (limits::max)() / 10) ||
         (value == (limits::max)() / 10 && digit > (limits::max)() % 10))
        throw std::overflow_error("integer overflow");
    }

    // 检查整数下溢 (负数方向)
    template<std::integral Integer>
    inline void check_underflow(Integer value, Integer digit) {
      using limits = std::numeric_limits<Integer>;
      if((value < (limits::min)() / 10) ||
         (value == (limits::min)() / 10 && digit < (limits::min)() % 10))
        throw std::underflow_error("integer underflow");
    }

    // 根据符号选择检查溢出还是下溢
    template<std::integral Integer>
    inline void
    check_over_underflow(Integer value, Integer digit, Integer sgn) {
      if(sgn == 1)
        check_overflow(value, digit);
      else
        check_underflow(value, digit);
    }

    // ===== 高性能整数数字解析 =====
    // 策略: 前 digits10 位保证不溢出，直接累加；之后才做溢出检查
    template<std::integral Integer, std::input_iterator Iter>
    inline Integer
    decode_digits(Iter &begin, Iter end, [[maybe_unused]] Integer sgn = 1) {
      assert(sgn == 1 || (std::is_signed_v<Integer> &&
                          std::make_signed_t<Integer>(sgn) == -1));

      Integer value = 0;

      // 快速路径: digits10 位数内绝对不会溢出，跳过检查
      for(int i = 0; i != std::numeric_limits<Integer>::digits10; i++) {
        if(begin == end)
          throw end_of_input_error();
        if(!std::isdigit(*begin))
          return value;

        if constexpr(std::is_signed_v<Integer>)
          value = value * 10 + (*begin++ - u8'0') * sgn;
        else
          value = value * 10 + (*begin++ - u8'0');
      }
      if(begin == end)
        throw end_of_input_error();

      // 慢速路径: 接近上限，需要逐位检查溢出
      if(std::isdigit(*begin)) {
        Integer digit;
        if constexpr(std::is_signed_v<Integer>) {
          digit = (*begin++ - u8'0') * sgn;
          check_over_underflow(value, digit, sgn);
        } else {
          digit = (*begin++ - u8'0');
          check_overflow(value, digit);
        }
        value = value * 10 + digit;
      }

      // 还有更多数字? 必然溢出
      if(std::isdigit(*begin)) {
        if(sgn == 1)
          throw std::overflow_error("integer overflow");
        else
          throw std::underflow_error("integer underflow");
      }

      return value;
    }

    // 解析 Bencode 整数: i<number>e
    template<std::integral Integer, std::input_iterator Iter>
    Integer decode_int(Iter &begin, Iter end) {
      assert(*begin == u8'i');
      ++begin; // 跳过 'i'
      Integer sgn = 1;
      if(*begin == u8'-') {
        if constexpr(std::is_unsigned_v<Integer>) {
          throw std::underflow_error("expected unsigned integer");
        } else {
          sgn = -1;
          ++begin; // 跳过 '-'
        }
      }

      Integer value = decode_digits<Integer>(begin, end, sgn);
      if(*begin != u8'e')
        throw syntax_error("expected 'e' token");

      ++begin; // 跳过 'e'
      return value;
    }

    // ===== 字符串/字节序列解析 (三个重载针对不同迭代器类型优化) =====

    // 重载1: forward_iterator → 可以直接计算距离，用范围构造
    template<typename String, std::forward_iterator Iter>
    String decode_chars(Iter &begin, Iter end, std::size_t len) {
      if(std::distance(begin, end) < static_cast<std::ptrdiff_t>(len)) {
        begin = end;
        throw end_of_input_error();
      }

      auto orig = begin;
      std::advance(begin, len);
      return String(orig, begin); // 范围构造，一次分配
    }

    // 重载2: input_iterator → 只能逐字符读取
    template<typename String, std::input_iterator Iter>
    inline String decode_chars(Iter &begin, Iter end, std::size_t len) {
      String value(len, 0); // 预分配
      for(std::size_t i = 0; i < len; i++) {
        if(begin == end)
          throw end_of_input_error();
        value[i] = *begin++;
      }
      return value;
    }

    // 重载3: contiguous_iterator + view → 零拷贝，直接指向原始缓冲区
    template<std::ranges::view String, std::contiguous_iterator Iter>
    String decode_chars(Iter &begin, Iter end, std::size_t len) {
      if(std::distance(begin, end) < static_cast<std::ptrdiff_t>(len)) {
        begin = end;
        throw end_of_input_error();
      }

      String value(&*begin, len); // string_view 构造，零拷贝!
      std::advance(begin, len);
      return value;
    }

    // 解析 Bencode 字符串: <length>:<data>
    template<typename String, std::input_iterator Iter>
    String decode_str(Iter &begin, Iter end) {
      assert(std::isdigit(*begin));
      std::size_t len = decode_digits<std::size_t>(begin, end); // 解析长度
      if(begin == end)
        throw end_of_input_error();
      if(*begin != u8':')
        throw syntax_error("expected ':' token");
      ++begin; // 跳过 ':'

      return decode_chars<String>(begin, end, len); // 根据迭代器类型选最优重载
    }

    // ===== 核心解码器: 基于显式栈的非递归解析 =====
    // all=true: 要求消耗完所有输入
    // all=false: 解析完一个完整值就停止(decode_some)
    template<typename Data, std::input_iterator Iter>
    Data do_decode(Iter &begin, Iter end, bool all) {
      using Traits = variant_traits_for<Data>;
      using Integer = typename Data::integer;
      using String  = typename Data::string;
      using List    = typename Data::list;
      using Dict    = typename Data::dict;

      Iter orig_begin = begin; // 记录起始位置，用于计算错误偏移
      String dict_key;         // 暂存当前正在解析的 dict key
      Data result;             // 最终结果
      std::stack<Data*> state; // 显式栈: 存储当前正在构建的容器指针

      // Lambda: 将刚解析的值存入正确的位置
      // 返回指向新存入元素的指针，以便后续嵌套时 push 到栈上
      auto store = [&result, &state, &dict_key](auto &&thing) -> Data * {
        if(state.empty()) {
          // 情况1: 根节点
          result = std::move(thing);
          return &result;
        } else if(auto p = Traits::template get_if<List>(state.top())) {
          // 情况2: 当前在 list 中 → push_back
          p->push_back(std::move(thing));
          return &p->back();
        } else if(auto p = Traits::template get_if<Dict>(state.top())) {
          // 情况3: 当前在 dict 中 → emplace with key
          auto i = p->emplace(std::move(dict_key), std::move(thing));
          if(!i.second) {
            throw syntax_error(
              "duplicated key in dict: " + std::string(i.first->first)
            );
          }
          return &i.first->second;
        }
        assert(false && "expected list or dict");
        return nullptr;
      };

      try {
        do {
          if(begin == end)
            throw end_of_input_error();

          if(*begin == u8'e') {
            // 'e' = 结束当前 list/dict
            if(!state.empty()) {
              ++begin;
              state.pop(); // 弹出当前容器，回到上层
            } else {
              throw syntax_error("unexpected 'e' token");
            }
          } else {
            // 如果当前在 dict 中，下一个token必须是 key(字符串)
            if(!state.empty() && Traits::index(*state.top()) == 3 /* dict */) {
              if(!std::isdigit(*begin))
                throw syntax_error("expected string start token for dict key");
              dict_key = detail::decode_str<String>(begin, end);
              if(begin == end)
                throw end_of_input_error();
            }

            // 根据首字符分发解析
            if(*begin == u8'i') {
              // 整数: i<number>e
              store(detail::decode_int<Integer>(begin, end));
            } else if(*begin == u8'l') {
              // 列表开始: 创建空 list，push 到栈上
              ++begin;
              state.push(store( List{} ));
            } else if(*begin == u8'd') {
              // 字典开始: 创建空 dict，push 到栈上
              ++begin;
              state.push(store( Dict{} ));
            } else if(std::isdigit(*begin)) {
              // 字符串: <len>:<data>
              store(detail::decode_str<String>(begin, end));
            } else {
              throw syntax_error("unexpected type token");
            }
          }
        } while(!state.empty()); // 栈空 = 顶层值解析完毕

        // 如果要求消耗全部输入，检查是否还有剩余
        if(all && begin != end)
          throw syntax_error("extraneous character");
      } catch(const std::exception &e) {
        // 包装为 decode_error，附带偏移量信息
        throw decode_error(e.what(), std::distance(orig_begin, begin),
                           std::current_exception());
      }

      return result;
    }

    // 从 istream 解码的重载
    template<typename Data>
    Data do_decode(std::istream &s, eof_behavior e, bool all) {
      // data_view 不能从流解码(string_view 无法持有流中的数据)
      static_assert(!std::ranges::view<typename Data::string>,
                    "reading from stream not supported for data views");

      std::istreambuf_iterator<char> begin(s), end;
      auto result = detail::do_decode<Data>(begin, end, all);
      // 如果读到了 EOF 且用户要求检查，设置流的 eofbit
      if(e == check_eof && begin == end)
        s.setstate(std::ios_base::eofbit);
      return result;
    }

  } // namespace detail

  // ========================================================================
  // 公共解码 API
  // ========================================================================

  // 从迭代器范围解码
  template<typename Data, std::input_iterator Iter>
  inline Data basic_decode(Iter begin, Iter end) {
    return detail::do_decode<Data>(begin, end, true);
  }

  // 从可迭代对象(string, vector<char> 等)解码
  template<typename Data, typename String>
  inline Data basic_decode(const String &s)
  requires(detail::iterable<String> && !std::is_array_v<String>) {
    return basic_decode<Data>(std::begin(s), std::end(s));
  }

  // 从 C 字符串解码
  template<typename Data>
  inline Data basic_decode(const char *s) {
    return basic_decode<Data>(s, s + std::strlen(s));
  }

  // 从 char* + 长度解码
  template<typename Data>
  inline Data basic_decode(const char *s, std::size_t length) {
    return basic_decode<Data>(s, s + length);
  }

  // 从流解码
  template<typename Data>
  inline Data basic_decode(std::istream &s, eof_behavior e = check_eof) {
    return detail::do_decode<Data>(s, e, true);
  }

  // decode_some 系列: 只解析一个完整值，不要求消耗全部输入
  template<typename Data, std::input_iterator Iter>
  inline Data basic_decode_some(Iter &begin, Iter end) {
    return detail::do_decode<Data>(begin, end, false);
  }

  template<typename Data>
  inline Data basic_decode_some(const char *&s) {
    return basic_decode_some<Data>(s, s + std::strlen(s));
  }

  template<typename Data>
  inline Data basic_decode_some(const char *&s, std::size_t length) {
    return basic_decode_some<Data>(s, s + length);
  }

  template<typename Data>
  inline Data basic_decode_some(std::istream &s, eof_behavior e = check_eof) {
    return detail::do_decode<Data>(s, e, false);
  }

  // ===== 便捷函数(自动推导 Data 类型) =====
  template<typename ...T>
  inline data decode(T &&...t) {
    return basic_decode<data>(std::forward<T>(t)...);
  }

  template<typename ...T>
  inline data decode_some(T &&...t) {
    return basic_decode_some<data>(std::forward<T>(t)...);
  }

  template<typename ...T>
  inline data_view decode_view(T &&...t) {
    return basic_decode<data_view>(std::forward<T>(t)...);
  }

  template<typename ...T>
  inline data_view decode_view_some(T &&...t) {
    return basic_decode_some<data_view>(std::forward<T>(t)...);
  }

#ifdef BENCODE_HAS_BOOST
  template<typename ...T>
  inline boost_data boost_decode(T &&...t) {
    return basic_decode<boost_data>(std::forward<T>(t)...);
  }

  template<typename ...T>
  inline boost_data boost_decode_some(T &&...t) {
    return basic_decode_some<boost_data>(std::forward<T>(t)...);
  }

  template<typename ...T>
  inline boost_data_view boost_decode_view(T &&...t) {
    return basic_decode<boost_data_view>(std::forward<T>(t)...);
  }

  template<typename ...T>
  inline boost_data_view boost_decode_view_some(T &&...t) {
    return basic_decode_some<boost_data_view>(std::forward<T>(t)...);
  }
#endif

  // ========================================================================
  // 编码实现
  // ========================================================================
  namespace detail {

    // RAII 列表编码器: 构造写 'l', 析构写 'e'
    template<std::input_or_output_iterator Iter>
    class list_encoder {
    public:
      inline list_encoder(Iter &iter) : iter(iter) {
        *iter++ = u8'l'; // 写入列表开始标记
      }

      inline ~list_encoder() {
        *iter++ = u8'e'; // 自动写入列表结束标记
      }

      template<typename T>
      inline list_encoder & add(T &&value); // 前向声明，后面定义
    private:
      Iter &iter;
    };

    // RAII 字典编码器: 构造写 'd', 析构写 'e'
    template<std::input_or_output_iterator Iter>
    class dict_encoder {
    public:
      inline dict_encoder(Iter &iter) : iter(iter) {
        *iter++ = u8'd'; // 写入字典开始标记
      }

      inline ~dict_encoder() {
        *iter++ = u8'e'; // 自动写入字典结束标记
      }

      template<typename T>
      inline dict_encoder & add(const string_view &key, T &&value);
    private:
      Iter &iter;
    };

    // 高性能整数→字符串写入(使用 std::to_chars)
    template<std::input_or_output_iterator Iter, typename T>
    Iter write_integer(Iter iter, T value) {
      // digits10 + 2: 足够容纳任何整数的十进制表示 + 负号
      char buf[std::numeric_limits<T>::digits10 + 2];
      auto r = std::to_chars(buf, buf + sizeof(buf), value);
      if(r.ec != std::errc())
        throw std::system_error(std::make_error_code(r.ec));
      return std::copy(buf, r.ptr, iter); // 复制到输出迭代器
    }
  } // namespace detail

  // ===== 编码单值函数 =====

  // 编码整数: i<number>e
  template<std::input_or_output_iterator Iter>
  inline Iter encode_to(Iter iter, integer value) {
    *iter++ = u8'i';
    iter = detail::write_integer(iter, value);
    *iter++ = u8'e';
    return iter;
  }

  // 编码类字符串对象: <len>:<data>
  template<std::input_or_output_iterator Iter, detail::stringish Str>
  requires(!std::is_array_v<Str>)
  inline Iter encode_to(Iter iter, const Str &value) {
    detail::write_integer(iter, std::size(value)); // 写入长度
    *iter++ = u8':';                                // 分隔符
    return std::copy(std::begin(value), std::end(value), iter); // 写入内容
  }

  // 编码 char* + 长度
  template<std::input_or_output_iterator Iter>
  inline Iter encode_to(Iter iter, const char *value, std::size_t length) {
    detail::write_integer(iter, length);
    *iter++ = u8':';
    return std::copy(value, value + length, iter);
  }

  // 编码字符数组(自动排除 '\0')
  template<std::input_or_output_iterator Iter, std::size_t N>
  inline Iter encode_to(Iter iter, const char (&value)[N]) {
    return encode_to(std::forward<Iter>(iter), value, N - 1);
  }

  // 编码可迭代序列 → list
  template<std::input_or_output_iterator Iter, detail::iterable Seq>
  Iter encode_to(Iter iter, const Seq &value) {
    detail::list_encoder e(iter); // RAII: 写入 'l'
    for(auto &&i : value)
      e.add(i);                   // 逐个编码元素
    return iter;                  // 析构时自动写入 'e'
  }

  // 编码映射 → dict
  template<std::input_or_output_iterator Iter, detail::mapping Map>
  Iter encode_to(Iter iter, const Map &value) {
    detail::dict_encoder e(iter); // RAII: 写入 'd'
    for(auto &&i : value)
      e.add(i.first, i.second);   // 逐个编码键值对
    return iter;                  // 析构时自动写入 'e'
  }

  // Visitor: 将 variant 的每种类型分发到对应的 encode_to
  namespace detail {
    template<std::input_or_output_iterator Iter>
    class encode_visitor {
    public:
      inline encode_visitor(Iter &iter) : iter(iter) {}

      template<typename T>
      void operator ()(T &&operand) const {
        encode_to(iter, std::forward<T>(operand));
      }
    private:
      Iter &iter;
    };
  } // namespace detail

  // 编码 basic_data (variant 类型)
  template<std::input_or_output_iterator Iter,
           template<typename ...> typename Variant, typename I, typename S,
           template<typename ...> typename L, template<typename ...> typename D>
  Iter encode_to(Iter iter, const basic_data<Variant, I, S, L, D> &value) {
    // 通过 visitor 模式自动识别 variant 中实际持有的类型并编码
    variant_traits<Variant>::visit(detail::encode_visitor(iter), value);
    return iter;
  }

  // list_encoder::add 的实现(必须在 encode_to 之后定义)
  namespace detail {
    template<std::input_or_output_iterator Iter>
    template<typename T>
    inline list_encoder<Iter> & list_encoder<Iter>::add(T &&value) {
      encode_to(iter, std::forward<T>(value));
      return *this; // 支持链式调用
    }

    template<std::input_or_output_iterator Iter>
    template<typename T>
    inline dict_encoder<Iter> &
    dict_encoder<Iter>::add(const string_view &key, T &&value) {
      encode_to(iter, key);                    // 先编码 key
      encode_to(iter, std::forward<T>(value)); // 再编码 value
      return *this;
    }
  } // namespace detail

  // ===== 便捷编码函数 =====

  // 编码到 std::string
  template<typename ...T>
  std::string encode(T &&...t) {
    std::stringstream ss;
    encode_to(std::ostreambuf_iterator(ss), std::forward<T>(t)...);
    return ss.str();
  }

  // 编码到 ostream
  template<typename ...T>
  std::ostream& encode_to(std::ostream &os, T &&...t) {
    encode_to(std::ostreambuf_iterator(os), std::forward<T>(t)...);
    return os;
  }

} // namespace bencode

#endif