#pragma once
#include "modulepack.hpp"
#include "components.hpp"


template <class Module, class... Ts>
class module_base
    : public module_pack<Module, cpack<Module>, meta::complete_protocol<Ts...>> {};

template <class Module,  class ... Ts>
class module_base<Module, Ts...> :
    public module_pack <Module, cpack<Module>, meta::complete_protocol<Ts...>> {};

template <class Module, class ... Ts>
class module_base< Module, meta::protocol<Ts...> > :
    public module_pack <Module, cpack<Module>, meta::complete_protocol<Ts...>> {};

template <class Module, class ... Ts>
class module_base< Module, meta::complete_protocol<Ts...> > :
    public module_pack <Module, cpack<Module>, meta::complete_protocol<Ts...>> {};

template <class Module, class Protocol, template <class> class ... Comp>
class module_component
    : public module_pack<Module, cpack<Module, Comp...>, Protocol> {};
