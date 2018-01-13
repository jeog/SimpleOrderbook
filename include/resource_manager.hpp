/*
Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see http://www.gnu.org/licenses.
*/

#ifndef JO_SOB_RESOURCE_MANAGER
#define JO_SOB_RESOURCE_MANAGER

#include <functional>
#include <iostream>
#include <sstream>

namespace sob {

template<typename T, class DeleterTy>
class ResourceManager_Debug;

template<typename T, class DeleterTy>
class ResourceManager {
    friend ResourceManager_Debug<T, DeleterTy>;
    DeleterTy _deleter;

public:
    typedef std::reference_wrapper<ResourceManager> ref_ty;
    typedef std::vector<ref_ty> relatives_ty;
    const std::string tag;

private:
    struct ResourceInfo{
        T * resource;
        const relatives_ty relatives;
        const DeleterTy deleter;
        /*
         * TODO: add fields for management, load balancing, diagnostics etc.
         */
        ResourceInfo( T * resource,
                      const relatives_ty& relatives,
                      DeleterTy deleter ) noexcept
            :
                resource(resource),
                relatives(relatives),
                deleter(deleter),
                owner(false)
            {
            }
        ~ResourceInfo()
            {
                if( owner ){
                    deleter(resource);
                }
            }

        inline void
        adopt()
        { owner = true; }

    private:
        bool owner;
    };

    std::unordered_map<T*, ResourceInfo> _resources;

    virtual bool
    _add(T * resource, const relatives_ty& relatives) noexcept
    {
        auto res = _resources.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(resource),
                std::forward_as_tuple(resource, relatives, _deleter)
        );
        return std::get<1>(res);
    }

    virtual bool
    _remove(T * resource) noexcept
    { return _resources.erase(resource) == 1; }


    ResourceManager(const ResourceManager& rm);
    ResourceManager(ResourceManager&& rm);
    ResourceManager operator=(const ResourceManager& rm);
    ResourceManager operator=(ResourceManager&& rm);

protected:
    ResourceManager(std::string tag, DeleterTy deleter)
        :
            _deleter( deleter ),
            tag(tag)
        {
        }

public:
    ResourceManager(std::string tag)
        :
            _deleter( DeleterTy(tag, "") ),
            tag(tag)
        {
        }

    virtual ~ResourceManager()
        {
            try{
                remove_all();
            }catch(...){
            }
        }

    bool
    add(T * resource /* = non-null */)
    {
        const relatives_ty relatives;
        return add(resource, relatives);
    }

    bool
    add(T * resource /* = non-null */, ResourceManager& relative)
    {
        const relatives_ty relatives = {relative};
        return add(resource, relatives);
    }

    bool
    add(T * resource /* = non-null */, const relatives_ty& relatives)
    {
        relatives_ty others;
        std::copy_if( relatives.cbegin(),
                      relatives.cend(),
                      std::insert_iterator<relatives_ty>(others, others.begin()),
                      [=](ref_ty o){ return &(o.get()) != this; } );

        if( !_add(resource, others) ){
            return false;
        }

        relatives_ty me = {*this}; // reciprocate the relation
        for( auto& r : others ){
            if( !(r.get()._add(resource, me)) ){
                remove(resource);
                return false;
            }
        }

        // if everything went OK take ownership of resource
        _resources.at(resource).adopt();
        return true;
    }

    void
    remove(T * resource) /* non-null */
    {
        auto fiter = _resources.find(resource);
        if( fiter != _resources.end() ){
            for( auto& rm : fiter->second.relatives ){
                rm.get()._remove(resource);
            }
            _remove(resource);
        }
    }

    void
    remove_all()
    {
        for( const auto& r : get_all() ){ // dont need copies
            remove(r);
        }
    }

    std::vector<T *>
    get_all() const
    {
        std::vector<T *> vec;
        for( const auto& p : _resources ){
            vec.push_back(p.second.resource);
        }
        return vec;
    }

    inline bool
    is_managed(T * resource)
    { return _resources.find(resource) != _resources.end(); }

};

template<typename T, class DeleterTy>
class ResourceManager_Debug
        : public ResourceManager<T,DeleterTy>{
    typedef ResourceManager<T,DeleterTy> base_ty;

    inline void
    _write(std::string msg) const
    {
        _out << "ResourceManager_Debug"
             << " :: " << base_ty::tag << " (" << std::hex << this << ") "
             << " :: " << msg << std::endl;
    }

    void
    _write(std::string msg, T *resource, bool success)
    {
        std::stringstream ss;
        ss << msg << " " << std::hex << (void*)resource
                  << " (" << std::boolalpha << success << ")";
        _write(ss.str());
    }

    bool
    _add( T *resource,
          const typename base_ty::relatives_ty& relatives ) noexcept
    {
        bool r = base_ty::_add(resource, relatives);
        _write("ADD", resource, r);
        return r;
    }

    bool
    _remove(T *resource) noexcept
    {
        bool r = base_ty::_remove(resource);
        _write("REMOVE", resource, r);
        return r;
    }

    std::ostream& _out;

public:
    ResourceManager_Debug(std::string tag, std::ostream& out=std::cout)
        :
            ResourceManager<T,DeleterTy>(tag, DeleterTy(tag, "SUCCESS", out) ),
            _out(out)
        {
            _write("CREATED");
        }

    ~ResourceManager_Debug()
        {
            _write("DESTROYED");
        }
};

}; /* sob */

#endif
