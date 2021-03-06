#pragma once
#include <type_traits>
#include <cstdint>
#include <tuple>

#define ARCHIVE_ASSERT(x)

namespace traits {

/// Checks if type is builtin or enum but not a pointer
template<typename T>
struct is_primitive {
    static const bool value = std::is_integral<T>::value
            || std::is_floating_point<T>::value
            || std::is_enum<T>::value;
};


/// Checks if given class looks like an stl container
template<typename T, typename = void>
struct is_container : std::false_type {};

template<typename T>
struct is_container<
        T,
        std::void_t<
            typename T::value_type,
            typename T::size_type,
            typename T::allocator_type,
            typename T::iterator,
            decltype(std::declval<T>().size()),
            decltype(std::declval<T>().begin()),
            decltype(std::declval<T>().end()),
            decltype(std::declval<T>().cbegin()),
            decltype(std::declval<T>().cend())
        >
> : public std::true_type {};


/// Checks if given class has dictionary-specific typedefs
template<typename T, typename = void>
struct is_key_value : std::false_type {};
template<typename T>
struct is_key_value<
        T,
        std::void_t<
            typename T::value_type,
            typename T::key_type,
            typename T::mapped_type
        >
> : public std::true_type {};


/// Get element type of container/array
template<typename T>
struct element_type {
	using type = std::remove_reference_t<decltype(*std::begin(std::declval<T&>()))>;
};

template<typename T>
using element_type_t = typename element_type<T>::type;


/// Get element type of container without const specifier for dictionaries
/// as for them value_type = std::pair<const Key, Val>
template<typename Container, typename = void, typename = void>
struct remove_const_element_type {};

template<typename Container>
struct remove_const_element_type<
			Container,
			std::enable_if_t<is_container<Container>::value>,
			std::enable_if_t<is_key_value<Container>::value>
		>
{
	using type = std::pair<
			typename std::remove_const<typename Container::key_type>::type,
			typename Container::mapped_type
	>;
};

template<typename Container>
struct remove_const_element_type<
			Container,
			std::enable_if_t<is_container<Container>::value>,
			std::enable_if_t<!is_key_value<Container>::value>
		>
{
	using type = typename Container::value_type;
};

template<typename T>
using remove_const_element_type_t = typename remove_const_element_type<T>::type;


/// Checks if given container can insert element using `insert(T)`
/// function without specifying mandatory position
template<typename Container, typename = void>
struct has_insert_value : std::false_type {};

template<typename Container>
struct has_insert_value<
        Container,
        std::void_t<decltype(std::declval<Container>().insert(std::declval<typename Container::value_type>()))>
> : std::true_type {};


/// Checks container for `push_back(T)`
template<typename Container, typename = void>
struct has_push_back : std::false_type {};

template<typename Container>
struct has_push_back<
        Container,
        std::void_t<decltype(std::declval<Container>().push_back(std::declval<typename Container::value_type>()))>
> : std::true_type {};


/// Checks container for `insert_after(It, T)`
template<typename Container, typename = void>
struct has_insert_after: std::false_type {};

template<typename Container>
struct has_insert_after<
        Container,
        std::void_t<decltype(std::declval<Container>().insert_after(std::declval<Container>().begin(), std::declval<typename Container::value_type>()))>
> : std::true_type {};


/// Checks container for `reserve(size_t)`
template<typename Container, typename = void>
struct has_reserve: std::false_type {};

template<typename Container>
struct has_reserve<
        Container,
        std::void_t<decltype(std::declval<Container>().reserve(0))>
> : std::true_type {};


/// Checks if `std::tuple_size<Type>` can be applyed to object
/// so it can be atreated as tuple and `std::get` can be applied
/// zero-element tuples intentonally give false
template<typename Type, typename = void>
struct is_tuple_like: std::false_type {};

template<typename Type>
struct is_tuple_like<
        Type,
		std::void_t<
			decltype(std::tuple_size<Type>::value),
			decltype(std::get<0>(std::declval<Type>()))
		>
> : std::true_type {};


/// Checks if given class looks like an stl optional
template<typename T, typename = void>
struct is_optional : std::false_type {};

template<typename T>
struct is_optional<
        T,
        std::void_t<
            typename T::value_type,
            decltype(std::declval<T>().reset()),
            decltype(std::declval<T>().operator -> ()),
            decltype(std::declval<T>().operator * ()),
            decltype(std::declval<const T>().operator bool ()),
            decltype(std::declval<const T>().has_value()),
            decltype(std::declval<const T>().value())
          >
> : public std::true_type {};


template<typename T> inline constexpr bool is_primitive_v = is_primitive<T>::value;
template<typename T> inline constexpr bool is_container_v = is_container<T>::value;
} // namespace traits


namespace archive {

/// A type used for objects sizes. `size_t` isn't suitable for the job
/// as it has different sizeof in different architectures
using usize = std::uint64_t;

namespace details {
/// Generalized `insert(Container, Type)` function to handle all possible differences
/// with STL collection interfaces
template<typename Container, typename T>
std::enable_if_t<traits::has_push_back<Container>::value> insert(Container& container, T&& t) {
    container.push_back(std::forward<T>(t));
}

template<typename Container, typename T>
std::enable_if_t<traits::has_insert_value<Container>::value> insert(Container& container, T&& t) {
    container.insert(std::forward<T>(t));
}

template<typename Container, typename T>
std::enable_if_t<traits::has_insert_after<Container>::value> insert(Container& container, T&& t) {
    container.insert_after(container.before_begin(), std::forward<T>(t));
}


/// Generalized `reserve(Container, size_t)` function to either reserve elements
/// or do nothing if given container cannot reserve space for elements
template<typename Container>
std::enable_if_t<traits::has_reserve<Container>::value> reserve_silent(Container& container, size_t size) {
    container.reserve(size);
}

template<typename Container>
void reserve_silent(Container&, ...) {}


struct InvalidType{};

/// Fallback helper for external_serialize_exists
template<typename T, typename P>
void serialize_object(T, P, ...);

/// Checks if there is a `serialize_object` function to serialize given type
/// using Archive class `P`
template<typename T, typename P>
struct external_serialize_exists {
    static const bool value = std::is_same<
            usize,
            decltype( serialize_object(std::declval<T&>(), std::declval<P&>()) )
    >::value;
};


/// Fallback helper for external_deserialize_exists
template<typename T, typename P>
InvalidType deserialize_object(T, P, ...);
/// Checks if there is an `deserialize_object` function to deserialize given type
/// using Archive class `P`
template<typename T, typename P>
struct external_deserialize_exists {
    static const bool value = std::is_same<
            void,
            decltype( deserialize_object(std::declval<T&>(), std::declval<P&>()) )
    >::value;
};



template <size_t I, size_t N, typename Tuple, typename Function>
constexpr void for_each_tuple_element(Tuple&& t, Function&& f) {
    if constexpr (I < N) {
        f(std::get<I>(t));
        for_each_tuple_element<I + 1, N>(std::forward<Tuple>(t), std::forward<Function>(f));
    }
    (void)(t);
    (void)(f);
}

} // namespace details


/// Storage policies for Archive
namespace storage_policy {

template<typename Archive>
struct Copy {
    Archive archive;
    Archive& get_storage() { return archive;}
};

template<typename Archive>
struct NotOwningPointer {
    Archive* archive;
    Archive& get_storage() { return *archive;}
};

} // namespace storage_policy


// TODO: contiguous primitive data deserialize optimization
template<typename Storage, typename StoragePolicy = storage_policy::Copy<Storage> >
struct BinaryArchive : public StoragePolicy {
    using StoragePolicy::get_storage;

    /// ===== Serialize =====

    template<typename Primitive>
    std::enable_if_t<traits::is_primitive<Primitive>::value, usize> serialize(const Primitive primitive) {
        return get_storage().write(reinterpret_cast<const char*>(&primitive), sizeof(Primitive));
    }

    template<typename Container>
    std::enable_if_t<traits::is_container<Container>::value || std::is_array<Container>::value, usize> serialize(const Container& container) {
        const size_t length = std::size(container);
        serialize<usize>(length);
        for (auto&& e: container) {
            serialize(e);
        }
        return sizeof(usize) + length * sizeof(traits::element_type_t<Container>);
    }

    template<typename Gettable>
    std::enable_if_t<
			traits::is_tuple_like<Gettable>::value
            && !traits::is_primitive<Gettable>::value
    , usize> serialize(const Gettable& object) {
        constexpr size_t N = std::tuple_size<Gettable>::value;
        usize size = 0;
        details::for_each_tuple_element<0, N>(object, [&size, this] (auto&& element) {
            size += serialize(element);
        });
        return size;
    }

    template<typename Serializable>
    std::enable_if_t<details::external_serialize_exists<Serializable, BinaryArchive<Storage, StoragePolicy>>::value, usize> serialize(const Serializable& object) {
        return serialize_object(object, *this);
    }

    template<typename Optional>
    std::enable_if_t<traits::is_optional<Optional>::value, usize> serialize(const Optional& optional) {
        usize size = serialize(optional.has_value());
        if (optional.has_value()) {
            size += serialize(*optional);
        }
        return size;
    }

    /// ===== Deserialize =====

    template<typename Primitive>
    std::enable_if_t<traits::is_primitive<Primitive>::value> deserialize(Primitive& primitive) {
        get_storage().read(reinterpret_cast<char*>(&primitive), sizeof(Primitive));
    }

    template<typename Container>
    std::enable_if_t<traits::is_container<Container>::value> deserialize(Container& container) {
        usize size = 0;
        deserialize(size);
        details::reserve_silent(container, static_cast<size_t>(size));

        for (size_t i = 0; i < size; ++i) {
            traits::remove_const_element_type_t<Container> e;
            deserialize(e);
            details::insert(container, std::move(e));
        }
    }

    template<typename Gettable>
    std::enable_if_t<
			traits::is_tuple_like<Gettable>::value
            && !traits::is_primitive<Gettable>::value
    > deserialize(Gettable& object) {
        details::for_each_tuple_element<0, std::tuple_size<Gettable>::value>(object, [this] (auto&& element) {
            deserialize(element);
        });
    }

    template<typename Deserializable>
    std::enable_if_t<details::external_deserialize_exists<Deserializable, BinaryArchive<Storage, StoragePolicy>>::value> deserialize(Deserializable& object) {
        deserialize_object(object, *this);
    }

    template<typename Optional>
    std::enable_if_t<traits::is_optional<Optional>::value> deserialize(Optional& optional) {
        bool has_value = false;
        deserialize(has_value);

        if (has_value) {
            std::remove_reference_t<decltype(*optional)> value;
            deserialize(value);
            optional = std::move(value);
        } else {
            optional.reset();
        }
    }


    template<typename T, size_t N>
    void deserialize(T(&array)[N]) {
        usize size = 0;
        deserialize(size);
		ARCHIVE_ASSERT(size == N);
        for (usize i = 0; i < N; ++i) {
            deserialize(array[i]);
        }
    }

};

} // namespace archive

#undef ARCHIVE_ASSERT
