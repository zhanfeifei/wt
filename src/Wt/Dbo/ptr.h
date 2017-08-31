// This may look like C code, but it's really -*- C++ -*-
/*
 * Copyright (C) 2008 Emweb bvba, Kessel-Lo, Belgium.
 *
 * See the LICENSE file for terms of use.
 */
#ifndef WT_DBO_DBO_PTR_H_
#define WT_DBO_DBO_PTR_H_

#include <string>
#include <Wt/cpp17/any.hpp>
#include <Wt/Dbo/SqlTraits.h>

#include <memory>
#include <sstream>
#include <type_traits>

namespace Wt {

  /*! \brief Namespace for the \ref dbo
   */
  namespace Dbo {

class Session;
class SqlStatement;
class SaveBaseAction;
template <class C> class collection;

namespace Impl {
  struct MappingInfo;

  extern WTDBO_API std::size_t ifind(const std::string& s,
				     const std::string& needle);

  struct WTDBO_API ParameterBase {
    virtual ~ParameterBase();
    virtual ParameterBase *clone() const = 0;
    virtual void bind(SaveBaseAction& binder) = 0;
  };

  template <typename T>
  struct Parameter : ParameterBase {
    Parameter(const T& v) : v_(v) { }

    virtual Parameter<T> *clone() const override;
    virtual void bind(SaveBaseAction& binder) override;

  private:
    T v_;
  };

  template<int N>
  struct Parameter<const char [N]> : Parameter<const char *> 
  {
    Parameter(char const *v) : Parameter<const char *>(v) { }
  };
  template<int N>
  struct Parameter<char [N]> : Parameter<const char *> 
  {
    Parameter(char const *v) : Parameter<const char *>(v) { }
  };
  
} // namespace Impl

class WTDBO_API MetaDboBase
{
public:
  enum State {
    // dbo state (also works with bitwise or)
    New = 0x000,
    Persisted = 0x001,
    Orphaned = 0x002,

    // flags
    NeedsDelete = 0x010,
    NeedsSave = 0x020,
    Saving = 0x040,

    DeletedInTransaction = 0x100,
    SavedInTransaction = 0x200,

    TransactionState = (SavedInTransaction | DeletedInTransaction)
  };

  MetaDboBase(int version, int state, Session *session)
    : session_(session), version_(version), state_(state), refCount_(0)
  { }

  virtual ~MetaDboBase();

  void transactionDone(bool success);

  virtual void flush() = 0;
  virtual Impl::MappingInfo *getMapping() = 0;
  virtual void doTransactionDone(bool success) = 0;
  virtual void bindId(SqlStatement *statement, int& column) = 0;
  virtual void bindModifyId(SqlStatement *statement, int& column) = 0;
  virtual void bindId(std::vector<Impl::ParameterBase *>& parameters) = 0;
  virtual void setAutogeneratedId(long long id) = 0;

  void setVersion(int version) { version_ = version; }
  virtual int version() const = 0;
  bool isTransient() const { return isNew() || isDeleted(); }

  void setSession(Session *session) { session_ = session; }
  Session *session() { return session_; }

  /*
   * Returns whether the object was not in the database prior
   * to the current transaction.
   */
  bool isNew() const { return 0 == (state_ & Persisted); }
  bool isPersisted() const
    { return 0 != (state_ & (Persisted | SavedInTransaction)); }
  bool isOrphaned() const { return 0 != (state_ & Orphaned); }
  bool isDeleted() const
    { return 0 != (state_ & (NeedsDelete | DeletedInTransaction)); }

  bool isDirty() const { return 0 != (state_ & NeedsSave); }
  bool inTransaction() const { return 0 != (state_ & 0xF00); }

  bool savedInTransaction() const
    { return 0 != (state_ & SavedInTransaction); }
  bool deletedInTransaction() const
    { return 0 != (state_ & DeletedInTransaction); }

  void setState(State state);

  void setDirty();
  void remove();

  void setTransactionState(State state);
  void resetTransactionState();

  void incRef();
  void decRef();

private:
  Session *session_;

protected:
  int version_;
  int state_;
  int refCount_;

  void checkNotOrphaned();
};

/*! \class dbo_default_traits Wt/Dbo/Dbo Wt/Dbo/Dbo
 *  \brief Default traits for a class mapped with %Wt::%Dbo.
 * 
 * This class provides the default traits. It is convenient (and
 * future proof) to inherit these default traits when customizing the
 * traits for one particular class.
 *
 * \ingroup dbo
 */
struct dbo_default_traits 
{
  /*! \brief Type of the primary key.
   *
   * The default corresponds to a surrogate key, which is <tt>long
   * long</tt>.
   */
  typedef long long IdType;

  /*! \brief Returns the sentinel value for a \c null id.
   *
   * The default implementation returns -1.
   */
  static IdType invalidId() { return -1; }

  /*! \brief Returns the database field name for the surrogate primary key.
   *
   * The default surrogate id database field name is <tt>"id"</tt>.
   */
  static const char *surrogateIdField() { return "id"; }

  /*! \brief Configures the optimistic concurrency version field.
   *
   * By default, optimistic concurrency locking is enabled using a
   * <tt>"version"</tt> field.
   */
  static const char *versionField() { return "version"; }
};

/*! \class dbo_traits Wt/Dbo/Dbo Wt/Dbo/Dbo
 *  \brief Traits for a class mapped with %Wt::%Dbo.
 *
 * The traits class provides some of the mapping properties related to
 * the primary key and optimistic concurrency locking using a version
 * field.
 *
 * See dbo_default_traits for default values.
 *
 * The following example changes the surrogate id field name for a
 * class <tt>Foo</tt> from the default <tt>"id"</tt> to
 * <tt>"foo_id"</tt>:
 *
 * \code
 * namespace Wt {
 *   namespace Dbo {
 *
 *     template<>
 *     struct dbo_traits<Foo> : dbo_default_traits
 *     {
 *        static const char *surrogateIdField() { return "foo_id"; }
 *     };
 *
 *     // Necessary if you want to use ptr<const Foo>
 *     template<> struct dbo_traits<const Foo> : dbo_traits<Foo> {};
 *   }
 * }
 * \endcode
 *
 * \note The safe pattern to define traits is before the class definition,
 *       based on a forward declaration.
 *       This is necessary since the persist() function relies on
 *       this specialization:
 * \code
 * class Foo;
 *
 * namespace Wt {
 *   namespace Dbo {
 *     template<> struct dbo_traits<Foo> : ... { };
 *   }
 * }
 *
 * class Foo {
 *   // definition here, including the persist() function
 * };
 * \endcode
 * \ingroup dbo
 */
template <class C>
struct dbo_traits : public dbo_default_traits
{
#ifdef DOXYGEN_ONLY
  /*! \brief Type of the primary key.
   *
   * This indicates the type of the primary key, which needs to be
   * <tt>long long</tt> for a surrogate id, but can be any type
   * supported by Wt::Dbo::field() (including composite types) for a
   * natural primary key.
   *
   * The following operations need to be supported for an id value:
   *
   *  - <i>default constructor</i>
   *  - <i>copy constructor</i>
   *  - serialization to a string (for formatting an error message in exceptions)
   *    : <tt>std::ostream << id</tt>
   *  - comparison operator (for use as a key in a std::map): <tt>id == id</tt>
   *  - less than operator (for use as a key in a std::map): <tt>id < id</tt>
   *
   * Only the default <tt>long long</tt> is supported for an
   * auto-incrementing surrogate primary key. You need to change the
   * default key type typically in conjuction with specifying a natural id,
   * see Wt::Dbo::id().
   *
   * The following example illustrates how to prepare a type to be
   * usable as a composite id type:
   *
   * \code
   * struct Coordinate {
   *   int x, y;
   *
   *   Coordinate()
   *     : x(-1), y(-1) { }
   *
   *   bool operator== (const Coordinate& other) const {
   *     return x == other.x && y == other.y;
   *   }
   *
   *   bool operator< (const Coordinate& other) const {
   *     if (x < other.x)
   *       return true;
   *     else if (x == other.x)
   *       return y < other.y;
   *     else
   *       return false;
   *   }
   * };
   * 
   * std::ostream& operator<< (std::ostream& o, const Coordinate& c)
   * {
   *   return o << "(" << c.x << ", " << c.y << ")";
   * }
   * 
   * namespace Wt {
   *   namespace Dbo {
   * 
   *     template <class Action>
   *     void field(Action& action, Coordinate& coordinate, const std::string& name, int size = -1)
   *     {
   *       field(action, coordinate.x, name + "_x");
   *       field(action, coordinate.y, name + "_y");
   *     }
   *   }
   * }
   * \endcode
   */
  typedef YourIdType IdType;

  /*! \brief Returns the sentinel value for a \c null id.
   *
   * When used as a foreign key, this value is used to represent a \c
   * null value.
   */
  static IdType invalidId();

  /*! \brief Configures the surrogate primary key field.
   *
   * Returns the field name which is the surrogate primary key,
   * corresponding to the object's id.
   *
   * You can disable this auto-incrementing surrogate id by returning
   * \c nullptr instead. In that case you will need to define a natural id
   * for your class using Wt::Dbo::id().
   */
  static const char *surrogateIdField();

  /*! \brief Configures the optimistic concurrency version field.
   *
   * Optimistic concurrency locking is used to detect concurrent
   * updates by an object from multiple sessions. On each update, the
   * version of a record is at the same time checked (to see if it
   * matches the version of the record that was read), and
   * incremented. A StaleObjectException is thrown if a record was
   * modified by another session since it was read.
   *
   * This method must return the database field name used for this
   * version field.
   *
   * You can disable optimistic locking using a version field all
   * together for your class by returning \c nullptr instead.
   */
  static const char *versionField();
#endif // DOXYGEN_ONLY
};

/*
  Manages a single object.
 */
template <class C>
class MetaDbo : public MetaDboBase
{
public:
  typedef typename dbo_traits<C>::IdType IdType;

  MetaDbo(C *obj);
  virtual ~MetaDbo();

  virtual Impl::MappingInfo *getMapping() override;
  virtual void flush() override;
  virtual void bindId(SqlStatement *statement, int& column) override;
  virtual void bindModifyId(SqlStatement *statement, int& column) override;
  virtual void bindId(std::vector<Impl::ParameterBase *>& parameters) override;
  virtual void setAutogeneratedId(long long id) override;

  void purge();
  void reread();
  virtual void doTransactionDone(bool success) override;

  bool isLoaded() const { return obj_ != nullptr; }
  C *obj();
  void setObj(C *obj);

  void setId(const IdType& id) { id_ = id; }
  IdType id() const { return id_; }
  std::string idStr() const {
    std::stringstream s;
    s << id();
    return s.str();
  }

  virtual int version() const override;

private:
  C     *obj_;
  IdType id_;

  MetaDbo(const IdType& idType, int version, int state, Session& session,
	  C *obj);
  MetaDbo(Session& session);

  void doLoad();
  void prune();

  friend class Session;
};

template <class C, class Enable = void>
struct DboHelper
{
  static void setMeta(C& /* c */, MetaDboBase * /* m */) { }
};

template <class C> class ptr;
template <class C> class weak_ptr;

/*! \class Dbo Wt/Dbo/Dbo Wt/Dbo/Dbo
 *  \brief A base class for database objects.
 *
 * The only requirement for a class to be be persisted is to have a \c
 * persist() method. In some cases however, it may be convenient to be
 * able to access database information of an object, such as its
 * database id and its session, from the object itself.
 *
 * By deriving your database class directly or indirectly from this
 * class, you can have access to its id() and session(). This will increase
 * the size of your object with one pointer.
 *
 * The following example shows a skeleton for a database object
 * which has access to its own id and session information:
 *
 * \code
 * class Cat : public Wt::Dbo::Dbo<Cat> {
 * public:
 *   template <class Action>
 *   void persist(Action& a) { }
 * };
 * \endcode
 *
 * Compare this to the skeleton for a minimum valid database class:
 *
 * \code
 * class Cat {
 * public:
 *   template <class Action>
 *   void persist(Action& a) { }
 * };
 * \endcode
 *
 * \ingroup dbo
 */
template <class C>
class Dbo
{
public:
  /*! \brief Constructor.
   */
  Dbo();

  /*! \brief Copy constructor.
   */
  Dbo(const Dbo<C>& other);

  /*! \brief Returns the database id.
   *
   * Returns the database id of this object, or
   * Wt::Dbo::dbo_traits<C>::invalidId() if the object is associated
   * with a session or not yet stored in the database.
   */
  typename dbo_traits<C>::IdType id() const;

  /*! \brief Returns the session.
   *
   * Returns the session to which this object belongs, or \c nullptr
   * if the object is not associated with a session.
   */
  Session *session() const;

  /*! \brief Marks the object as modified.
   *
   * When accessing a database object using ptr.modify(), the object
   * is marked as dirty. Any intermediate query will however flush the
   * current transaction and other changes within a member method will
   * not be recorded.
   *
   * You can call this method to achieve the same as ptr.modify() but
   * from within member methods.
   */
  void setDirty();

  /*! \brief Returns whether this object is dirty.
   *
   * \sa setDirty()
   */
  bool isDirty() const;

  /*! \brief Returns a dbo::ptr to this object.
   *
   * The returned pointer points to the current object only if there
   * exists at least one other pointer to the object. Otherwise it
   * returns a \c null ptr.
   *
   * This means that in practice you should adopt the habit of wrapping
   * a newly created database object directly in a ptr (and perhaps also
   * add it to a session):
   */
  ptr<C> self() const;
  
private:
  MetaDbo<C> *meta_;

  template <class D, class Enable> friend struct DboHelper;
};

template <class C>
struct DboHelper<C, typename std::enable_if<std::is_base_of<Dbo<C>, C>::value>::type>
{
  static void setMeta(C& obj, MetaDbo<C> *m) { obj.meta_ = m; }
};

class WTDBO_API ptr_base
{
public:
  ptr_base() { }
  virtual ~ptr_base();

  virtual void transactionDone(bool success) = 0;
};

template <class C>
std::ostream& operator<< (std::ostream& o, const ptr<C>& ptr);

/*! \defgroup dbo Database Objects (Wt::Dbo)
 *  \brief An implemenation of an Object Relational Mapping layer.
 *
 * For an introduction, see the <a href="../../tutorial/dbo.html">tutorial</a>.
 */

/*! \class ptr Wt/Dbo/ptr.h Wt/Dbo/ptr.h
 *  \brief A smart pointer for a database object.
 *
 * This smart pointer class implements a reference counted shared
 * pointer for database objects, which also keeps tracking of
 * synchronization between the in-memory copy and the database
 * copy. You should always use this pointer class to reference a database
 * object.
 *
 * Unlike typical C++ data structures, classes mapped to database
 * tables do not have clear ownership relationships. Therefore, the
 * conventional ownership-based memory allocation/deallocation does
 * not work naturally for database classes.
 *
 * A pointer may point to a <i>transient</i> object or a
 * <i>persisted</i> object. A persisted object has a corresponding
 * copy in the database while a transient object is only present in
 * memory. To persist a new object, use Session::add(). To make a
 * persisted object transient, use remove().
 *
 * Unlike a typical smart pointer, this pointer only allows read
 * access to the underlying object by default. To modify the object,
 * you should explicitly use modify(). This is used to mark the
 * underyling object as <i>dirty</i> to add it to the queue of objects
 * to be synchronized with the database.
 *
 * The pointer class provides a number of methods to deal with the
 * persistence state of the object:
 * - id(): returns the database id
 * - flush(): forces the object to be synchronized to the database
 * - remove(): deletes the object in the underlying database
 * - reread(): rereads the database copy of the object
 * - purge(): purges the transient version of a non-dirty object.
 *
 * Wt::Dbo::ptr<const C> can be used when retrieving query results.
 * There are overloads for the copy constructor, copy assignment,
 * and comparison operators to make this work as expected.
 *
 * \ingroup dbo
 */
template <class C>
class ptr : public ptr_base
{
private:
  typedef typename std::remove_const<C>::type MutC;
public:
  typedef C pointed;

  class mutator
  {
  public:
    mutator(MetaDbo<MutC> *obj);
    ~mutator();

    C *operator->() const;
    C& operator*() const;
    operator C*() const;

  private:
    MetaDbo<MutC> *obj_;
  };

  ptr();

  ptr(std::nullptr_t);

  /*! \brief Creates a new pointer.
   *
   * When \p obj is not \c nullptr, the pointer points to the new
   * unpersisted object. Use Session::add() to persist the newly
   * created object.
   */
  ptr(std::unique_ptr<C> obj);

  /*! \brief Copy constructor.
   */
  ptr(const ptr<C>& other);

  template <class D>
  ptr(const ptr<D>& other);

  /*! \brief Destructor.
   *
   * This method will delete the transient copy of the database object if
   * it is not referenced by any other pointer.
   */
  virtual ~ptr();

  /*! \brief Resets the pointer.
   *
   * This is equivalent to:
   * \code
   * p = ptr<C>(std::move(obj));
   * \endcode
   */
  void reset(std::unique_ptr<C> obj = nullptr);

  /*! \brief Assignment operator.
   */
  ptr<C>& operator= (const ptr<C>& other);

  template <class D>
  ptr<C>& operator= (const ptr<D>& other);

  /*! \brief Dereference operator.
   *
   * Note that this operator returns a const copy of the referenced
   * object. Use modify() to get a non-const reference.
   *
   * Since this may lazy-load the underlying database object, you
   * should have an active transaction.
   */
  const C *operator->() const;

  /*! \brief Returns the pointer.
   *
   * Note that returns a const pointer. Use modify() to get a non-const
   * pointer.
   *
   * Since this may lazy-load the underlying database object, you
   * should have an active transaction.
   *
   * \sa modify()
   */
  const C *get() const;

  /*! \brief Dereference operator.
   *
   * Note that this operator returns a const copy of the referenced
   * object. Use modify() to get a non-const reference.
   *
   * Since this may lazy-load the underlying database object, you
   * should have an active transaction.
   */
  const C& operator*() const;

  /*! \brief Dereference operator, for writing.
   *
   * Returns the underlying object (or, rather, a proxy for it) with
   * the intention to modify it. The proxy object will mark the object
   * as dirty from its destructor. An involved modification should
   * therefore preferably be implemented as a separate method or
   * function to make sure that the object is marked as dirty after the
   * whole modification:
   * \code
   *   ptr<A> a = ...;
   *   a.modify()->doSomething();
   * \endcode
   *
   * Since this may lazy-load the underlying database object, you
   * should have an active transaction.
   *
   * \sa get()
   */
#ifdef DOXYGEN_ONLY
  C *modify() const;
#else
  mutator modify() const;
#endif // DOXYGEN_ONLY

  /*! \brief Comparison operator.
   *
   * Two pointers are equal if and only if they reference the same
   * database object.
   */
#ifdef DOXYGEN_ONLY
  bool operator== (const ptr<C>& other) const;
#else
  bool operator== (const ptr<MutC>& other) const;
  bool operator== (const ptr<const C>& other) const;
#endif // DOXYGEN_ONLY

  /*! \brief Comparison operator.
   *
   * Two pointers are equal if and only if they reference the same
   * database object.
   *
   * Since this needs to query the value, you should have an active transaction.
   */
#ifdef DOXYGEN_ONLY
  bool operator== (const weak_ptr<C>& other) const;
#else
  bool operator== (const weak_ptr<MutC>& other) const;
  bool operator== (const weak_ptr<const C>& other) const;
#endif // DOXYGEN_ONLY

  /*! \brief Comparison operator.
   *
   * Two pointers are equal if and only if they reference the same
   * database object.
   */
#ifdef DOXYGEN_ONLY
  bool operator!= (const ptr<C>& other) const;
#else
  bool operator!= (const ptr<MutC>& other) const;
  bool operator!= (const ptr<const C>& other) const;
#endif // DOXYGEN_ONLY

  /*! \brief Comparison operator.
   *
   * Two pointers are equal if and only if they reference the same
   * database object.
   *
   * Since this needs to query the value, you should have an active transaction.
   */
#ifdef DOXYGEN_ONLY
  bool operator!= (const weak_ptr<C>& other) const;
#else
  bool operator!= (const weak_ptr<MutC>& other) const;
  bool operator!= (const weak_ptr<const C>& other) const;
#endif // DOXYGEN_ONLY

  /*! \brief Comparison operator.
   *
   * This operator is implemented to be able to store pointers in
   * std::set or std::map containers.
   */
#ifdef DOXYGEN_ONLY
  bool operator< (const ptr<C>& other) const;
#else
  bool operator< (const ptr<MutC>& other) const;
  bool operator< (const ptr<const C>& other) const;
#endif // DOXYGEN_ONLY

  /*! \brief Checks for null.
   *
   * Returns true if the pointer is pointing to a non-null object.
   */
  explicit operator bool() const;

  /*! \brief Flushes the object.
   *
   * If dirty, the object is synchronized to the database. This will
   * automatically also flush objects that are referenced by this
   * object if needed. The object is not actually committed to the
   * database before the active transaction has been committed.
   *
   * Since this may persist object to the database, you should have an
   * active transaction.
   */
  void flush() const;

  /*! \brief Removes an object from the database.
   *
   * The object is removed from the database, and becomes transient again.
   *
   * Note that the object is not deleted in memory: you can still
   * continue to read and modify the object, but there will no longer
   * be a database copy of the object, and the object will effectively
   * be treated as a new object (which may be re-added to the database
   * at a later point).
   *
   * This is the opposite operation of Session::add().
   */
  void remove();

  /*! \brief Rereads the database version.
   *
   * Rereads a persisted object from the database, discarding any
   * possible changes and updating to the latest database version.
   *
   * This does not actually load the database version, since loading is
   * lazy.
   */
  void reread();

  /*! \brief Purges an object from memory.
   *
   * When the object is not dirty, the memory copy of the object is deleted,
   * and the object will be reread from the database on the next access.
   *
   * Purging an object can be useful to conserve memory, but you should never
   * purge an object while the user is editing if you wish to rely on the
   * optimistick locking for detecting concurrent modifications.
   */
  void purge();

  /*! \brief Returns the object id.
   *
   * This returns dbo_traits<C>::invalidId() for a transient object.
   */
  typename dbo_traits<C>::IdType id() const;

  /*! \brief Returns the object version.
   *
   * This returns -1 for a transient object or when versioning is not
   * enabled.
   */
  int version() const;

  /*! \brief Returns whether the object is transient.
   *
   * This returns true for a transient object.
   */
  bool isTransient() const;

  /*! \brief Returns whether the object is dirty.
   *
   * A dirty object will be flushed whenever a query is made or the current
   * transaction ends.
   */
  bool isDirty() const;

  /*! \brief Returns the session with which this pointer is associated.
   *
   * This may return \c nullptr if the pointer is null or not added to
   * a session.
   */
  Session *session() const;

protected:
  MetaDbo<MutC> *obj() const { return obj_; }

private:
  MetaDbo<MutC> *obj_;

  ptr(MetaDbo<MutC> *obj);

  void takeObj();
  void freeObj();

  void resetObj(MetaDboBase *dbo);
  virtual void transactionDone(bool success) override;
  
  friend class Session;
  friend class SaveBaseAction;
  friend class ToAnysAction;
  friend class FromAnyAction;
  friend class SetReciproceAction;
  friend class Dbo<C>;
  friend class ptr<MutC>;
  friend class ptr<const C>;
  template <class D> friend class collection;

  friend std::ostream& operator<< <> (std::ostream& o, const ptr<C>& ptr);
};

/*! \brief Make a new ptr
 *
 * This is a shorthand for
 * \code ptr<T>(std::unique_ptr<T>(new T(...))) \endcode
 *
 * \sa Wt::Dbo::Session::addNew()
 */
template<typename T, typename ...Args>
ptr<T> make_ptr( Args&& ...args )
{
  return ptr<T>(std::unique_ptr<T>(new T(std::forward<Args>(args)...)));
}

template <class C>
struct query_result_traits< ptr<C> >
{
  static void getFields(Session& session,
			std::vector<std::string> *aliases,
			std::vector<FieldInfo>& result);

  static ptr<C> load(Session& session, SqlStatement& statement,
		     int& column);

  static void getValues(const ptr<C>& ptr, std::vector<cpp17::any>& values);
  static void setValue(const ptr<C>& ptr, int& index, const cpp17::any& value);

  static ptr<C> create();
  static void add(Session& session, ptr<C>& ptr);
  static void remove(ptr<C>& ptr);

  static long long id(const ptr<C>& ptr);
  static ptr<C> findById(Session& session, long long id);
};

  }
}

#endif // WT_DBO_DBO_PTR_H_
