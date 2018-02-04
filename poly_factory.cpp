#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <cstdlib>
#include <cxxabi.h>

std::string demangle(const char *name) {

  int status = -4; // some arbitrary value to eliminate the compiler warning

  std::unique_ptr<char, void (*)(void *)> res{
      abi::__cxa_demangle(name, NULL, NULL, &status), std::free};

  return (status == 0) ? res.get() : name;
}

template <class Rt, class... Args>
struct function_ptr_factory {
  using return_type = Rt;

private:
  using func_type = return_type (*)(Args...);

public:
  function_ptr_factory(func_type func_ptr) : m_func_ptr(func_ptr) {}

  template <class T>
  static function_ptr_factory create() {
    return +[](Args... args) -> return_type {
      return std::make_unique<T>(std::forward<Args>(args)...);
    };
  }

  template <class... Ts>
  return_type operator()(Ts &&... ts) {
    return m_func_ptr(std::forward<Ts>(ts)...);
  }

private:
  func_type m_func_ptr;
};

template <class key, class ft>
struct simple_storage {
  using factory_type = ft;

  template <class... Ts>
  static auto make(const key &k, Ts &&... args) {
    return data().at(k)(std::forward<Ts>(args)...);
  }

  static void add(key k, factory_type f) { data().emplace(k, f); }

private:
  static auto &data() {
    static std::unordered_map<key, factory_type> s;
    return s;
  }
};

template <class storage_type>
struct rtti_registration {

  template <class derived>
  static bool register_derived() {
    const auto name = demangle(typeid(derived).name());
    auto factory = storage_type::factory_type::template create<derived>();
    storage_type::add(name, factory);
    return true;
  }
};

template <class storage, class factory, class registration_policy, bool strict,
          class Base, class... Args>
class basic_poly_factory {
public:
  template <class... T>
  static typename factory::return_type make(T &&... args) {
    return storage::make(std::forward<T>(args)...);
  }

  template <class T, class B = Base>
  struct Registrar : B {
    friend T;

    static bool registerT() {
      return registration_policy::template register_derived<T>();
    }
    static bool registered;

  private:
    template <bool b = strict, std::enable_if_t<b, int> = 0>
    Registrar() : B(Key{}) {
      (void)registered;
    }
      template <bool b = strict, std::enable_if_t<!b, int> = 0>
      Registrar() : B() {
          (void)registered;
      }
  };

  friend Base;

private:
  class Key {
    Key(){};
    template <class T, class B>
    friend struct Registrar;
  };
  using FuncType = std::unique_ptr<Base> (*)(Args...);
  basic_poly_factory() = default;
};

template <class storage, class factory, class registration_policy, bool strict,
          class Base, class... Args>
template <class T, class B>
bool basic_poly_factory<storage, factory, registration_policy, strict, Base,
                        Args...>::template Registrar<T, B>::registered =
    basic_poly_factory<storage, factory, registration_policy, strict, Base,
                       Args...>::template Registrar<T, B>::registerT();

template <bool strict, class Base, class... Args>
using poly_factory = basic_poly_factory<
    simple_storage<std::string,
                   function_ptr_factory<std::unique_ptr<Base>, Args...>>,
    function_ptr_factory<std::unique_ptr<Base>, Args...>,
    rtti_registration<simple_storage<
        std::string, function_ptr_factory<std::unique_ptr<Base>, Args...>>>,
    strict, Base, Args...>;

struct Animal : poly_factory<true, Animal, int> {
  Animal(Key) {}
  virtual void makeNoise() = 0;
};

class Dog : public Animal::Registrar<Dog> {
public:
  Dog(int x) : m_x(x) {}

  void makeNoise() { std::cerr << "Dog: " << m_x << "\n"; }

private:
  int m_x;
};

class Cat : public Animal::Registrar<Cat> {
public:
  Cat(int x) : m_x(x) {}

  void makeNoise() { std::cerr << "Cat: " << m_x << "\n"; }

private:
  int m_x;
};

// Won't compile because of the private CRTP constructor
// class Spider : public Animal::Registrar<Cat> {
// public:jessica mcnamee
//     Spider(int x) : m_x(x) {}

//     void makeNoise() { std::cerr << "Spider: " << m_x << "\n"; }

// private:
//     int m_x;
// };

// Won't compile because of the pass key idiom
// class Zob : public Animal {
// public:
//     Zob(int x) : Animal({}), m_x(x) {}

//      void makeNoise() { std::cerr << "Zob: " << m_x << "\n"; }
//     std::unique_ptr<Animal> clone() const { return
//     std::make_unique<Zob>(*this); }

// private:
//      int m_x;

// };

// An example that shows that rvalues can be passed in as arguments
// correctly.
struct Creature : poly_factory<true, Creature, std::unique_ptr<int>> {
  Creature(Key) {}
  virtual void makeNoise() = 0;
};

class Ghost : public Creature::Registrar<Ghost> {
public:
  Ghost(std::unique_ptr<int> &&x) : m_x(*x) {}

  void makeNoise() { std::cerr << "Ghost: " << m_x << "\n"; }

private:
  int m_x;
};

// Non-strict example
struct Thought : poly_factory<false, Thought, int> {
    Thought() = default;
    virtual void makeNoise() = 0;
    virtual std::unique_ptr<Thought> clone() const = 0;
};

template <class T>
struct cloner : Thought {
    std::unique_ptr<Thought> clone() const override {
        return std::make_unique<T>(static_cast<const T&>(*this));
    }
};

class Happy : public Thought::Registrar<Happy, cloner<Happy>> {
public:
    Happy(int x) : m_x(x) {}

    void makeNoise() override { std::cerr << "Happy: " << m_x << "\n"; }

private:
    int m_x;
};

int main() {
  auto x = Animal::make("Dog", 3);
  auto y = Animal::make("Cat", 2);
  x->makeNoise();
  y->makeNoise();
  auto z = Creature::make("Ghost", std::make_unique<int>(4));

  auto p = Thought::make("Happy", 3);
  p->makeNoise();
  auto q = p->clone();
  q->makeNoise();
  return 0;
}
