// DBPFX - declarative binary grammar
//
// A binary format is declared once, as a type:
//
//     using NameMap = seq<
//         field<"version", u32>,
//         field<"count",   count_of<"names", u32>>,
//         field<"names",   array<NameEntry, ref<"count">>>>;
//
// From that single declaration the compiler generates:
//   * lazy, zero-copy readers  -> parse<NameMap>(bytes).get<"names">()[3].get<"text">()
//     Nothing is decoded until a field is asked for; offsets of fixed-size prefixes are
//     folded at compile time, variable-size ones are measured on demand.
//   * owned models             -> view.own() gives a mutable model<NameMap>
//   * serializers              -> serialize<NameMap>(model); length/count fields are
//     recomputed automatically from the data they describe.
//
// Readers never copy: integers are loaded straight from the mapped bytes, UTF-8 text is
// returned as std::string_view into the file, UTF-16 text as a view decoded on request.
#pragma once

#include "platform.hpp"

#include <array>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

namespace dbpfx::g {

inline constexpr std::size_t dyn = static_cast<std::size_t>(-1);

// ============================================================================ helpers
template <class T>
struct is_optional : std::false_type {};
template <class T>
struct is_optional<std::optional<T>> : std::true_type {};

template <class T>
[[nodiscard]] constexpr std::uint64_t to_u64(const T& v) {
    if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
        return static_cast<std::uint64_t>(v);
    else if constexpr (is_optional<T>::value)
        return v ? to_u64(*v) : 0;
    else
        static_assert(sizeof(T) == 0, "referenced field is not an integer");
}

// ============================================================================ contexts
// Contexts form a by-value chain (field -> enclosing seq -> ... -> root), so any view can
// be copied and kept around for as long as the underlying bytes live.

/// Root of every read: optional named values supplied from outside the blob
/// (e.g. the index entry count lives in the package header, not in the index).
struct root_ctx {
    struct var {
        std::string_view name;
        std::uint64_t value;
    };
    std::array<var, 4> vars{};
    std::size_t n = 0;

    root_ctx() = default;
    root_ctx(std::initializer_list<var> v) {
        for (const auto& x : v)
            if (n < vars.size()) vars[n++] = x;
    }
    template <fixed_string N>
    [[nodiscard]] std::uint64_t lookup() const {
        for (std::size_t i = 0; i < n; ++i)
            if (vars[i].name == N.view()) return vars[i].value;
        throw format_error("unresolved grammar reference '" + std::string(N.view()) + "'");
    }
};

/// Root of every write.
struct root_mctx {
    root_ctx vars;
    template <fixed_string N>
    [[nodiscard]] std::uint64_t lookup() const { return vars.template lookup<N>(); }
    template <fixed_string N>
    [[nodiscard]] std::uint64_t length_of() const {
        throw format_error("count_of target '" + std::string(N.view()) + "' not found");
    }
};

template <class S, class P>
struct seq_ctx;
template <class S, class P>
struct model_ctx;
template <class S, class P>
class view;
template <class S>
struct model;

// ============================================================================ codec helpers
template <class C, class M>
[[nodiscard]] std::uint64_t effective(const typename C::owned_type& v, const M& mctx) {
    if constexpr (requires { C::effective(v, mctx); })
        return C::effective(v, mctx);
    else
        return to_u64(v);
}

// ============================================================================ primitives
/// Little-endian scalar.
template <class T>
struct le {
    template <class>
    using value_t = T;
    using owned_type = T;
    static constexpr std::size_t fixed_size = sizeof(T);

    template <class C>
    static std::size_t measure(const C&, const std::byte*, const std::byte*) { return sizeof(T); }
    template <class C>
    static T read(const C&, const std::byte* p, const std::byte* e) {
        need(p, e, sizeof(T), "scalar");
        return load_le<T>(p);
    }
    template <class M>
    static void write(writer& w, const T& v, const M&) { w.put<T>(v); }
    template <class C>
    static T own(const T& v) { return v; }
};

using u8 = le<std::uint8_t>;
using u16 = le<std::uint16_t>;
using u32 = le<std::uint32_t>;
using u64 = le<std::uint64_t>;
using i32 = le<std::int32_t>;
using f32 = le<float>;

/// Constant signature bytes. Reading validates, writing emits the literal.
template <fixed_string M>
struct magic {
    template <class>
    using value_t = std::string_view;
    using owned_type = std::monostate;
    static constexpr std::size_t fixed_size = M.size();

    template <class C>
    static std::size_t measure(const C&, const std::byte*, const std::byte*) { return M.size(); }
    template <class C>
    static std::string_view read(const C&, const std::byte* p, const std::byte* e) {
        need(p, e, M.size(), "magic");
        std::string_view got(reinterpret_cast<const char*>(p), M.size());
        if (got != M.view()) {
            std::string shown;
            for (char ch : got) {
                if (ch >= 0x20 && ch < 0x7F) shown += ch;
                else {
                    static const char* hex = "0123456789ABCDEF";
                    shown += "\\x";
                    shown += hex[(static_cast<unsigned char>(ch) >> 4) & 0xF];
                    shown += hex[static_cast<unsigned char>(ch) & 0xF];
                }
            }
            throw format_error("bad magic: expected '" + std::string(M.view()) + "', found '" + shown + "'");
        }
        return got;
    }
    template <class Mc>
    static void write(writer& w, const std::monostate&, const Mc&) { w.put_bytes(M.data, M.size()); }
    template <class C>
    static std::monostate own(std::string_view) { return {}; }
};

/// Fixed block of opaque bytes (reserved/padding), preserved verbatim on round-trip.
template <std::size_t N>
struct raw {
    template <class>
    using value_t = byte_span;
    using owned_type = std::array<std::byte, N>;
    static constexpr std::size_t fixed_size = N;

    template <class C>
    static std::size_t measure(const C&, const std::byte*, const std::byte*) { return N; }
    template <class C>
    static byte_span read(const C&, const std::byte* p, const std::byte* e) {
        need(p, e, N, "raw block");
        return {p, N};
    }
    template <class M>
    static void write(writer& w, const owned_type& v, const M&) { w.put_bytes(v.data(), N); }
    template <class C>
    static owned_type own(byte_span s) {
        owned_type o{};
        std::copy_n(s.data(), N, o.data());
        return o;
    }
};

/// Integer whose value is derived on write from the length of a sibling field.
/// `Int` is an integer codec such as u32.
template <fixed_string Target, class Int>
struct count_of : Int {
    using T = typename Int::owned_type;
    template <class M>
    static void write(writer& w, const T&, const M& mctx) {
        w.put<T>(static_cast<T>(mctx.template length_of<Target>()));
    }
    template <class M>
    static std::uint64_t effective(const T&, const M& mctx) { return mctx.template length_of<Target>(); }
};

/// Integer whose value is computed on write by Fn::compute(model) (checksums, byte totals...).
template <class Int, class Fn>
struct computed : Int {
    using T = typename Int::owned_type;
    template <class M>
    static void write(writer& w, const T&, const M& mctx) { w.put<T>(static_cast<T>(Fn::compute(mctx.owner()))); }
    template <class M>
    static std::uint64_t effective(const T&, const M& mctx) { return Fn::compute(mctx.owner()); }
};

// ============================================================================ length specs
template <fixed_string N>
struct ref {
    static constexpr bool is_rest = false;
    template <class C>
    static std::uint64_t eval(const C& c) { return c.template lookup<N>(); }
};
template <std::uint64_t N>
struct lit {
    static constexpr bool is_rest = false;
    template <class C>
    static std::uint64_t eval(const C&) { return N; }
};
/// "Everything up to the end of the enclosing blob".
struct rest {
    static constexpr bool is_rest = true;
};

// ============================================================================ text & blobs
/// UTF-16LE text as stored by The Sims 3 / Medieval. Decoding happens only on request.
struct u16_view {
    const std::byte* p = nullptr;
    std::size_t n = 0;  // code units

    [[nodiscard]] std::size_t size() const { return n; }
    [[nodiscard]] char16_t at(std::size_t i) const { return static_cast<char16_t>(load_le<std::uint16_t>(p + 2 * i)); }
    [[nodiscard]] std::u16string str() const {
        std::u16string s(n, u'\0');
        for (std::size_t i = 0; i < n; ++i) s[i] = at(i);
        return s;
    }
    [[nodiscard]] std::string utf8() const;
};

std::string utf16_to_utf8(std::u16string_view s);
std::u16string utf8_to_utf16(std::string_view s);
inline std::string u16_view::utf8() const { return utf16_to_utf8(str()); }

template <class Len>
struct utf16 {
    template <class>
    using value_t = u16_view;
    using owned_type = std::u16string;
    static constexpr std::size_t fixed_size = dyn;

    template <class C>
    static std::size_t units(const C& c, const std::byte* p, const std::byte* e) {
        if constexpr (Len::is_rest) return static_cast<std::size_t>(e - p) / 2;
        else return static_cast<std::size_t>(Len::eval(c));
    }
    template <class C>
    static std::size_t measure(const C& c, const std::byte* p, const std::byte* e) {
        const auto n = units(c, p, e);
        need(p, e, n * 2, "UTF-16 text");
        return n * 2;
    }
    template <class C>
    static u16_view read(const C& c, const std::byte* p, const std::byte* e) { return {p, measure(c, p, e) / 2}; }
    template <class M>
    static void write(writer& w, const std::u16string& s, const M&) {
        for (char16_t ch : s) w.put<std::uint16_t>(static_cast<std::uint16_t>(ch));
    }
    template <class C>
    static std::u16string own(const u16_view& v) { return v.str(); }
};

/// UTF-8 text as stored by The Sims 4. Returned as a view straight into the bytes.
template <class Len>
struct utf8 {
    template <class>
    using value_t = std::string_view;
    using owned_type = std::string;
    static constexpr std::size_t fixed_size = dyn;

    template <class C>
    static std::size_t measure(const C& c, const std::byte* p, const std::byte* e) {
        std::size_t n;
        if constexpr (Len::is_rest) n = static_cast<std::size_t>(e - p);
        else n = static_cast<std::size_t>(Len::eval(c));
        need(p, e, n, "UTF-8 text");
        return n;
    }
    template <class C>
    static std::string_view read(const C& c, const std::byte* p, const std::byte* e) {
        return {reinterpret_cast<const char*>(p), measure(c, p, e)};
    }
    template <class M>
    static void write(writer& w, const std::string& s, const M&) { w.put_bytes(s.data(), s.size()); }
    template <class C>
    static std::string own(std::string_view v) { return std::string(v); }
};

template <class Len>
struct bytes {
    template <class>
    using value_t = byte_span;
    using owned_type = byte_vec;
    static constexpr std::size_t fixed_size = dyn;

    template <class C>
    static std::size_t measure(const C& c, const std::byte* p, const std::byte* e) {
        std::size_t n;
        if constexpr (Len::is_rest) n = static_cast<std::size_t>(e - p);
        else n = static_cast<std::size_t>(Len::eval(c));
        need(p, e, n, "byte block");
        return n;
    }
    template <class C>
    static byte_span read(const C& c, const std::byte* p, const std::byte* e) { return {p, measure(c, p, e)}; }
    template <class M>
    static void write(writer& w, const byte_vec& v, const M&) { w.put_bytes(v.data(), v.size()); }
    template <class C>
    static byte_vec own(byte_span s) { return byte_vec(s.begin(), s.end()); }
};

// ============================================================================ conditional
/// Field present only when (ref & Mask) != 0 (or == 0 when Invert).
template <fixed_string Ref, std::uint64_t Mask, class C, bool Invert = false>
struct when {
    template <class Ctx>
    using value_t = std::optional<typename C::template value_t<Ctx>>;
    using owned_type = std::optional<typename C::owned_type>;
    static constexpr std::size_t fixed_size = dyn;

    template <class Ctx>
    static bool present(const Ctx& c) { return ((c.template lookup<Ref>() & Mask) != 0) != Invert; }

    template <class Ctx>
    static std::size_t measure(const Ctx& c, const std::byte* p, const std::byte* e) {
        return present(c) ? C::measure(c, p, e) : 0;
    }
    template <class Ctx>
    static value_t<Ctx> read(const Ctx& c, const std::byte* p, const std::byte* e) {
        if (!present(c)) return std::nullopt;
        return C::read(c, p, e);
    }
    template <class M>
    static void write(writer& w, const owned_type& v, const M& mctx) {
        if (!present(mctx)) return;
        C::write(w, v ? *v : typename C::owned_type{}, mctx);
    }
    template <class Ctx>
    static owned_type own(const value_t<Ctx>& v) {
        if (!v) return std::nullopt;
        return C::template own<Ctx>(*v);
    }
};
template <fixed_string Ref, std::uint64_t Mask, class C>
using unless = when<Ref, Mask, C, true>;

// ============================================================================ arrays
/// Marks a variable-layout element whose size is nonetheless identical for every element of
/// one array (e.g. DBPF index entries): the stride is measured once, then access is O(1).
template <class C>
struct uniform : C {
    static constexpr bool is_uniform = true;
};
template <class C>
concept uniform_codec = requires { C::is_uniform; };

template <class Elem, class Ctx>
class array_view {
public:
    using value_type = typename Elem::template value_t<Ctx>;

    array_view() = default;
    array_view(const std::byte* p, const std::byte* e, std::uint64_t n, std::size_t stride, Ctx c)
        : p_(p), e_(e), n_(n), stride_(stride), ctx_(c) {}

    [[nodiscard]] std::size_t size() const { return static_cast<std::size_t>(n_); }
    [[nodiscard]] bool empty() const { return n_ == 0; }
    [[nodiscard]] bool random_access() const { return stride_ != dyn; }

    [[nodiscard]] value_type operator[](std::size_t i) const {
        if (i >= n_) throw std::out_of_range("array_view index");
        if (stride_ != dyn) return Elem::read(ctx_, p_ + i * stride_, e_);
        auto it = begin();
        for (std::size_t k = 0; k < i; ++k) ++it;
        return *it;
    }
    /// Raw pointer to element i (only for random-access arrays).
    [[nodiscard]] const std::byte* element_data(std::size_t i) const { return p_ + i * stride_; }
    [[nodiscard]] std::size_t stride() const { return stride_; }

    class iterator {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = typename array_view::value_type;
        iterator() = default;
        iterator(const array_view* v, const std::byte* cur, std::uint64_t i) : v_(v), cur_(cur), i_(i) {}
        value_type operator*() const { return Elem::read(v_->ctx_, cur_, v_->e_); }
        iterator& operator++() {
            cur_ += v_->stride_ != dyn ? v_->stride_ : Elem::measure(v_->ctx_, cur_, v_->e_);
            ++i_;
            return *this;
        }
        iterator operator++(int) {
            auto t = *this;
            ++*this;
            return t;
        }
        bool operator==(const iterator& o) const { return i_ == o.i_; }
        [[nodiscard]] const std::byte* data() const { return cur_; }

    private:
        const array_view* v_ = nullptr;
        const std::byte* cur_ = nullptr;
        std::uint64_t i_ = 0;
    };
    [[nodiscard]] iterator begin() const { return {this, p_, 0}; }
    [[nodiscard]] iterator end() const { return {this, nullptr, n_}; }

private:
    const std::byte* p_ = nullptr;
    const std::byte* e_ = nullptr;
    std::uint64_t n_ = 0;
    std::size_t stride_ = dyn;
    Ctx ctx_{};
};

template <class Elem, class Len>
struct array {
    template <class Ctx>
    using value_t = array_view<Elem, Ctx>;
    using owned_type = std::vector<typename Elem::owned_type>;
    static constexpr std::size_t fixed_size = dyn;

    template <class Ctx>
    static value_t<Ctx> read(const Ctx& c, const std::byte* p, const std::byte* e) {
        std::uint64_t n = 0;
        std::size_t stride = dyn;
        if constexpr (Elem::fixed_size != dyn) stride = Elem::fixed_size;
        if constexpr (Len::is_rest) {
            static_assert(Elem::fixed_size != dyn, "rest-length arrays need fixed-size elements");
            n = static_cast<std::uint64_t>(e - p) / stride;
        } else {
            n = Len::eval(c);
        }
        if constexpr (Elem::fixed_size == dyn && uniform_codec<Elem>)
            if (n > 0) stride = Elem::measure(c, p, e);
        if (stride != dyn) {
            if (stride != 0 && n > static_cast<std::uint64_t>(e - p) / stride)
                throw format_error("array count exceeds available data");
        } else if (n > static_cast<std::uint64_t>(e - p)) {
            throw format_error("array count exceeds available data");
        }
        return {p, e, n, stride, c};
    }
    template <class Ctx>
    static std::size_t measure(const Ctx& c, const std::byte* p, const std::byte* e) {
        auto v = read(c, p, e);
        if (v.random_access()) return v.size() * v.stride();
        std::size_t total = 0;
        for (auto it = v.begin(); it != v.end(); ++it) total += Elem::measure(c, it.data(), e);
        return total;
    }
    template <class M>
    static void write(writer& w, const owned_type& v, const M& mctx) {
        for (const auto& x : v) Elem::write(w, x, mctx);
    }
    template <class Ctx>
    static owned_type own(const value_t<Ctx>& v) {
        owned_type o;
        o.reserve(v.size());
        for (auto x : v) o.push_back(Elem::template own<Ctx>(x));
        return o;
    }
};

// ============================================================================ records
template <fixed_string Name, class C>
struct field {
    static constexpr auto name = Name;
    using codec = C;
};

template <class... F>
struct seq {
    using fields = std::tuple<F...>;
    static constexpr std::size_t count = sizeof...(F);
    static constexpr std::array<std::string_view, count> names{F::name.view()...};

    template <std::size_t I>
    using codec_at = typename std::tuple_element_t<I, fields>::codec;

    template <fixed_string N>
    static constexpr std::size_t index_of() {
        for (std::size_t i = 0; i < count; ++i)
            if (names[i] == N.view()) return i;
        return count;
    }
    template <fixed_string N>
    static constexpr bool has = index_of<N>() < count;

    template <std::size_t I>
    static constexpr std::size_t static_offset() {
        std::size_t off = 0;
        bool ok = true;
        [&]<std::size_t... K>(std::index_sequence<K...>) {
            ((codec_at<K>::fixed_size == dyn ? (ok = false) : (off += codec_at<K>::fixed_size, true)), ...);
        }(std::make_index_sequence<I>{});
        return ok ? off : dyn;
    }
    static constexpr std::size_t fixed_size = static_offset<count>();

    template <class Ctx>
    using value_t = view<seq, Ctx>;
    using owned_type = model<seq>;

    /// Offset of field I relative to the record start (compile-time when possible).
    template <std::size_t I, class SC>
    static std::size_t offset_in(const SC& sc, const std::byte* base, const std::byte* end) {
        if constexpr (static_offset<I>() != dyn) {
            return static_offset<I>();
        } else {
            const std::size_t prev = offset_in<I - 1>(sc, base, end);
            return prev + codec_at<I - 1>::measure(sc, base + prev, end);
        }
    }

    template <fixed_string N, class P>
    static auto read_field(const P& parent, const std::byte* base, const std::byte* end) {
        constexpr std::size_t I = index_of<N>();
        static_assert(I < count, "no such field in this record");
        const seq_ctx<seq, P> sc{base, end, parent};
        return codec_at<I>::read(sc, base + offset_in<I>(sc, base, end), end);
    }

    template <class P>
    static std::size_t measure(const P& parent, const std::byte* base, const std::byte* end) {
        if constexpr (fixed_size != dyn) {
            return fixed_size;
        } else {
            const seq_ctx<seq, P> sc{base, end, parent};
            return offset_in<count>(sc, base, end);
        }
    }

    template <class P>
    static view<seq, P> read(const P& parent, const std::byte* p, const std::byte* e) {
        if constexpr (fixed_size != dyn) need(p, e, fixed_size, "record");
        return view<seq, P>(p, e, parent);
    }

    template <class M>
    static void write(writer& w, const model<seq>& m, const M& parent) {
        const model_ctx<seq, M> mc{&m, parent};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (codec_at<I>::write(w, std::get<I>(m.values), mc), ...);
        }(std::make_index_sequence<count>{});
    }

    template <class Ctx>
    static model<seq> own(const view<seq, Ctx>& v) { return v.own(); }
};

template <class S, class P>
struct seq_ctx {
    const std::byte* base;
    const std::byte* end;
    P parent;

    template <fixed_string N>
    [[nodiscard]] std::uint64_t lookup() const {
        if constexpr (S::template has<N>)
            return to_u64(S::template read_field<N>(parent, base, end));
        else
            return parent.template lookup<N>();
    }
};

template <class S, class P>
struct model_ctx {
    const model<S>* m;
    P parent;

    [[nodiscard]] const model<S>& owner() const { return *m; }

    template <fixed_string N>
    [[nodiscard]] std::uint64_t lookup() const {
        if constexpr (S::template has<N>) {
            constexpr std::size_t I = S::template index_of<N>();
            return effective<typename S::template codec_at<I>>(std::get<I>(m->values), *this);
        } else {
            return parent.template lookup<N>();
        }
    }
    template <fixed_string N>
    [[nodiscard]] std::uint64_t length_of() const {
        if constexpr (S::template has<N>)
            return static_cast<std::uint64_t>(std::get<S::template index_of<N>()>(m->values).size());
        else
            return parent.template length_of<N>();
    }
};

template <class S>
struct owned_tuple;
template <class... F>
struct owned_tuple<seq<F...>> {
    using type = std::tuple<typename F::codec::owned_type...>;
};

/// Mutable, owning counterpart of a record view. Built by view::own() or by hand.
template <class S>
struct model {
    typename owned_tuple<S>::type values{};

    template <fixed_string N>
    [[nodiscard]] auto& get() { return std::get<S::template index_of<N>()>(values); }
    template <fixed_string N>
    [[nodiscard]] const auto& get() const { return std::get<S::template index_of<N>()>(values); }
    template <fixed_string N, class V>
    model& set(V&& v) {
        get<N>() = std::forward<V>(v);
        return *this;
    }
};

/// Lazy record view. Holds only pointers; every get<>() decodes exactly one field.
template <class S, class P>
class view {
public:
    using schema = S;
    view() = default;
    view(const std::byte* base, const std::byte* end, P parent) : base_(base), end_(end), parent_(parent) {}

    template <fixed_string N>
    [[nodiscard]] auto get() const { return S::template read_field<N>(parent_, base_, end_); }

    template <fixed_string N>
    [[nodiscard]] std::size_t offset() const {
        const seq_ctx<S, P> sc{base_, end_, parent_};
        return S::template offset_in<S::template index_of<N>()>(sc, base_, end_);
    }
    [[nodiscard]] std::size_t size() const { return S::measure(parent_, base_, end_); }
    [[nodiscard]] const std::byte* data() const { return base_; }
    [[nodiscard]] byte_span bytes() const { return {base_, size()}; }
    [[nodiscard]] seq_ctx<S, P> ctx() const { return {base_, end_, parent_}; }

    [[nodiscard]] model<S> own() const {
        model<S> m;
        const auto sc = ctx();
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((std::get<I>(m.values) = S::template codec_at<I>::template own<seq_ctx<S, P>>(
                  S::template codec_at<I>::read(sc, base_ + S::template offset_in<I>(sc, base_, end_), end_))),
             ...);
        }(std::make_index_sequence<S::count>{});
        return m;
    }

    /// Calls f(name, value) for every field, in declaration order.
    template <class Fn>
    void visit(Fn&& f) const {
        const auto sc = ctx();
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (f(S::names[I], S::template codec_at<I>::read(sc, base_ + S::template offset_in<I>(sc, base_, end_), end_)), ...);
        }(std::make_index_sequence<S::count>{});
    }

private:
    const std::byte* base_ = nullptr;
    const std::byte* end_ = nullptr;
    P parent_{};
};

// ============================================================================ alternatives
template <std::size_t Offset, class T>
struct peek {
    static T at(const std::byte* p, const std::byte* e) {
        need(p, e, Offset + sizeof(T), "discriminator");
        return load_le<T>(p + Offset);
    }
};
template <auto V, class C>
struct case_ {
    static constexpr auto value = V;
    using codec = C;
};

/// Picks one of several layouts by peeking at bytes (e.g. a version number).
template <class Peek, class... Cases>
struct one_of {
    template <class Ctx>
    using value_t = std::variant<typename Cases::codec::template value_t<Ctx>...>;
    using owned_type = std::variant<typename Cases::codec::owned_type...>;
    static constexpr std::size_t fixed_size = dyn;

    template <class Ctx, std::size_t I = 0>
    static value_t<Ctx> read(const Ctx& c, const std::byte* p, const std::byte* e) {
        if constexpr (I == sizeof...(Cases)) {
            throw format_error("unsupported layout version");
        } else {
            using K = std::tuple_element_t<I, std::tuple<Cases...>>;
            const auto d = Peek::at(p, e);
            if (d == static_cast<decltype(d)>(K::value))
                return value_t<Ctx>(std::in_place_index<I>, K::codec::read(c, p, e));
            return read<Ctx, I + 1>(c, p, e);
        }
    }
    template <class Ctx>
    static std::size_t measure(const Ctx& c, const std::byte* p, const std::byte* e) {
        return std::visit([](const auto& v) { return v.size(); }, read(c, p, e));
    }
    template <class M>
    static void write(writer& w, const owned_type& v, const M& mctx) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            ((v.index() == I ? (std::tuple_element_t<I, std::tuple<Cases...>>::codec::write(w, std::get<I>(v), mctx), 0) : 0), ...);
        }(std::make_index_sequence<sizeof...(Cases)>{});
    }
    template <class Ctx>
    static owned_type own(const value_t<Ctx>& v) {
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            owned_type o;
            ((v.index() == I ? (o = owned_type(std::in_place_index<I>,
                                               std::tuple_element_t<I, std::tuple<Cases...>>::codec::template own<Ctx>(std::get<I>(v))),
                                0)
                             : 0),
             ...);
            return o;
        }(std::make_index_sequence<sizeof...(Cases)>{});
    }
};

// ============================================================================ entry points
template <class S>
[[nodiscard]] auto parse(byte_span b, root_ctx ctx = {}) {
    return S::read(ctx, b.data(), b.data() + b.size());
}

template <class S>
[[nodiscard]] byte_vec serialize(const typename S::owned_type& m, root_ctx ctx = {}) {
    byte_vec out;
    writer w(out);
    S::write(w, m, root_mctx{ctx});
    return out;
}

template <class S>
void serialize_into(writer& w, const typename S::owned_type& m, root_ctx ctx = {}) {
    S::write(w, m, root_mctx{ctx});
}

}  // namespace dbpfx::g
