/* Author: Wolfgang Bangerth, University of Heidelberg, 2000 */

/*    $Id: step-9.cc 28462 2013-02-19 15:25:50Z bangerth $       */
/*                                                                */
/*    Copyright (C) 2000-2004, 2006-2008, 2010-2013 by the deal.II authors */
/*                                                                */
/*    This file is subject to QPL and may not be  distributed     */
/*    without copyright and license information. Please refer     */
/*    to the file deal.II/doc/license.html for the  text  and     */
/*    further information on this license.                        */

// Just as in previous examples, we have to include several files of which the
// meaning has already been discussed:
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>
#include <deal.II/base/logstream.h>
#include <deal.II/lac/vector.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/sparse_matrix.h>
#include <deal.II/lac/solver_bicgstab.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/grid/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_refinement.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/grid/tria_boundary_lib.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/data_out.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/grid/grid_out.h>

// The following two files provide classes and information for multi-threaded
// programs. In the first one, the classes and functions are declared which we
// need to start new threads and to wait for threads to return (i.e. the
// <code>Thread</code> class and the <code>new_thread</code> functions). The
// second file has a class <code>MultithreadInfo</code> (and a global object
// <code>multithread_info</code> of that type) which can be used to query the
// number of processors in your system, which is often useful when deciding
// how many threads to start in parallel.
#include <deal.II/base/thread_management.h>
#include <deal.II/base/multithread_info.h>

// The next new include file declares a base class <code>TensorFunction</code>
// not unlike the <code>Function</code> class, but with the difference that
// the return value is tensor-valued rather than scalar of vector-valued.
#include <deal.II/base/tensor_function.h>

#include <deal.II/numerics/error_estimator.h>

// This is C++, as we want to write some output to disk:
#include <fstream>
#include <iostream>


// The last step is as in previous programs:
namespace Step9
{
  using namespace dealii;

  // @sect3{AdvectionProblem class declaration}

  // Following we declare the main class of this program. It is very much
  // alike the main classes of previous examples, so we again only comment on
  // the differences.
  template <int dim>
  class AdvectionProblem
  {
  public:
    AdvectionProblem ();
    ~AdvectionProblem ();
    void run ();

  private:
    void setup_system ();
    // The next function will be used to assemble the matrix. However, unlike
    // in the previous examples, the function will not do the work itself, but
    // rather it will split the range of active cells into several chunks and
    // then call the following function on each of these chunks. The rationale
    // is that matrix assembly can be parallelized quite well, as the
    // computation of the local contributions on each cell is entirely
    // independent of other cells, and we only have to synchronize when we add
    // the contribution of a cell to the global matrix. The second function,
    // doing the actual work, accepts two parameters which denote the first
    // cell on which it shall operate, and the one past the last.
    //
    // The strategy for parallelization we choose here is one of the
    // possibilities mentioned in detail in the @ref threads module in the
    // documentation. While it is a straightforward way to distribute the work
    // for assembling the system onto multiple processor cores. As mentioned
    // in the module, there are other, and possibly better suited, ways to
    // achieve the same goal.
    void assemble_system ();
    void assemble_system_interval (const typename DoFHandler<dim>::active_cell_iterator &begin,
                                   const typename DoFHandler<dim>::active_cell_iterator &end);

    // The following functions again are as in previous examples, as are the
    // subsequent variables.
    void solve ();
    void refine_grid ();
    void output_results (const unsigned int cycle) const;

    Triangulation<dim>   triangulation;
    DoFHandler<dim>      dof_handler;

    FE_Q<dim>            fe;

    ConstraintMatrix     hanging_node_constraints;

    SparsityPattern      sparsity_pattern;
    SparseMatrix<double> system_matrix;

    Vector<double>       solution;
    Vector<double>       system_rhs;

    // When assembling the matrix in parallel, we have to synchronize when
    // several threads attempt to write the local contributions of a cell to
    // the global matrix at the same time. This is done using a
    // <code>Mutex</code>, which is an object that can be owned by only one
    // thread at a time. If a thread wants to write to the matrix, it has to
    // acquire this lock (if it is presently owned by another thread, then it
    // has to wait), then write to the matrix and finally release the
    // lock. Note that if the library was not compiled to support
    // multithreading (which you have to specify at the time you call the
    // <code>./configure</code> script in the top-level directory), then
    // the actual data type of the typedef
    // <code>Threads::Mutex</code> is a dummy class that provides all the
    // functions needed for a mutex, but does nothing when they are called;
    // this is reasonable, of course, since if only one thread is running at a
    // time, there is no need to synchronize with other threads.
    Threads::Mutex     assembler_lock;
  };



  // @sect3{Equation data declaration}

  // Next we declare a class that describes the advection field. This, of
  // course, is a vector field with as many compents as there are space
  // dimensions. One could now use a class derived from the
  // <code>Function</code> base class, as we have done for boundary values and
  // coefficients in previous examples, but there is another possibility in
  // the library, namely a base class that describes tensor valued
  // functions. In contrast to the usual <code>Function</code> objects, we
  // provide the compiler with knowledge on the size of the objects of the
  // return type. This enables the compiler to generate efficient code, which
  // is not so simple for usual vector-valued functions where memory has to be
  // allocated on the heap (thus, the <code>Function::vector_value</code>
  // function has to be given the address of an object into which the result
  // is to be written, in order to avoid copying and memory allocation and
  // deallocation on the heap). In addition to the known size, it is possible
  // not only to return vectors, but also tensors of higher rank; however,
  // this is not very often requested by applications, to be honest...
  //
  // The interface of the <code>TensorFunction</code> class is relatively
  // close to that of the <code>Function</code> class, so there is probably no
  // need to comment in detail the following declaration:
  template <int dim>
  class AdvectionField : public TensorFunction<1,dim>
  {
  public:
    AdvectionField () : TensorFunction<1,dim> () {}

    virtual Tensor<1,dim> value (const Point<dim> &p) const;

    virtual void value_list (const std::vector<Point<dim> > &points,
                             std::vector<Tensor<1,dim> >    &values) const;

    // In previous examples, we have used assertions that throw exceptions in
    // several places. However, we have never seen how such exceptions are
    // declared. This can be done as follows:
    DeclException2 (ExcDimensionMismatch,
                    unsigned int, unsigned int,
                    << "The vector has size " << arg1 << " but should have "
                    << arg2 << " elements.");
    // The syntax may look a little strange, but is reasonable. The format is
    // basically as follows: use the name of one of the macros
    // <code>DeclExceptionN</code>, where <code>N</code> denotes the number of
    // additional parameters which the exception object shall take. In this
    // case, as we want to throw the exception when the sizes of two vectors
    // differ, we need two arguments, so we use
    // <code>DeclException2</code>. The first parameter then describes the
    // name of the exception, while the following declare the data types of
    // the parameters. The last argument is a sequence of output directives
    // that will be piped into the <code>std::cerr</code> object, thus the
    // strange format with the leading <code>@<@<</code> operator and the
    // like. Note that we can access the parameters which are passed to the
    // exception upon construction (i.e. within the <code>Assert</code> call)
    // by using the names <code>arg1</code> through <code>argN</code>, where
    // <code>N</code> is the number of arguments as defined by the use of the
    // respective macro <code>DeclExceptionN</code>.
    //
    // To learn how the preprocessor expands this macro into actual code,
    // please refer to the documentation of the exception classes in the base
    // library. Suffice it to say that by this macro call, the respective
    // exception class is declared, which also has error output functions
    // already implemented.
  };



  // The following two functions implement the interface described above. The
  // first simply implements the function as described in the introduction,
  // while the second uses the same trick to avoid calling a virtual function
  // as has already been introduced in the previous example program. Note the
  // check for the right sizes of the arguments in the second function, which
  // should always be present in such functions; it is our experience that
  // many if not most programming errors result from incorrectly initialized
  // arrays, incompatible parameters to functions and the like; using
  // assertion as in this case can eliminate many of these problems.
  template <int dim>
  Tensor<1,dim>
  AdvectionField<dim>::value (const Point<dim> &p) const
  {
    Point<dim> value;
    value[0] = 2;
    for (unsigned int i=1; i<dim; ++i)
      value[i] = 1+0.8*std::sin(8*numbers::PI*p[0]);

    return value;
  }



  template <int dim>
  void
  AdvectionField<dim>::value_list (const std::vector<Point<dim> > &points,
                                   std::vector<Tensor<1,dim> >    &values) const
  {
    Assert (values.size() == points.size(),
            ExcDimensionMismatch (values.size(), points.size()));

    for (unsigned int i=0; i<points.size(); ++i)
      values[i] = AdvectionField<dim>::value (points[i]);
  }




  // Besides the advection field, we need two functions describing the source
  // terms (<code>right hand side</code>) and the boundary values. First for
  // the right hand side, which follows the same pattern as in previous
  // examples. As described in the introduction, the source is a constant
  // function in the vicinity of a source point, which we denote by the
  // constant static variable <code>center_point</code>. We set the values of
  // this center using the same template tricks as we have shown in the step-7
  // example program. The rest is simple and has been shown previously,
  // including the way to avoid virtual function calls in the
  // <code>value_list</code> function.
  template <int dim>
  class RightHandSide : public Function<dim>
  {
  public:
    RightHandSide () : Function<dim>() {}

    virtual double value (const Point<dim>   &p,
                          const unsigned int  component = 0) const;

    virtual void value_list (const std::vector<Point<dim> > &points,
                             std::vector<double>            &values,
                             const unsigned int              component = 0) const;

  private:
    static const Point<dim> center_point;
  };


  template <>
  const Point<1> RightHandSide<1>::center_point = Point<1> (-0.75);

  template <>
  const Point<2> RightHandSide<2>::center_point = Point<2> (-0.75, -0.75);

  template <>
  const Point<3> RightHandSide<3>::center_point = Point<3> (-0.75, -0.75, -0.75);



  // The only new thing here is that we check for the value of the
  // <code>component</code> parameter. As this is a scalar function, it is
  // obvious that it only makes sense if the desired component has the index
  // zero, so we assert that this is indeed the
  // case. <code>ExcIndexRange</code> is a global predefined exception
  // (probably the one most often used, we therefore made it global instead of
  // local to some class), that takes three parameters: the index that is
  // outside the allowed range, the first element of the valid range and the
  // one past the last (i.e. again the half-open interval so often used in the
  // C++ standard library):
  template <int dim>
  double
  RightHandSide<dim>::value (const Point<dim>   &p,
                             const unsigned int  component) const
  {
    Assert (component == 0, ExcIndexRange (component, 0, 1));
    const double diameter = 0.1;
    return ( (p-center_point).square() < diameter*diameter ?
             .1/std::pow(diameter,dim) :
             0);
  }



  template <int dim>
  void
  RightHandSide<dim>::value_list (const std::vector<Point<dim> > &points,
                                  std::vector<double>            &values,
                                  const unsigned int              component) const
  {
    Assert (values.size() == points.size(),
            ExcDimensionMismatch (values.size(), points.size()));

    for (unsigned int i=0; i<points.size(); ++i)
      values[i] = RightHandSide<dim>::value (points[i], component);
  }



  // Finally for the boundary values, which is just another class derived from
  // the <code>Function</code> base class:
  template <int dim>
  class BoundaryValues : public Function<dim>
  {
  public:
    BoundaryValues () : Function<dim>() {}

    virtual double value (const Point<dim>   &p,
                          const unsigned int  component = 0) const;

    virtual void value_list (const std::vector<Point<dim> > &points,
                             std::vector<double>            &values,
                             const unsigned int              component = 0) const;
  };



  template <int dim>
  double
  BoundaryValues<dim>::value (const Point<dim>   &p,
                              const unsigned int  component) const
  {
    Assert (component == 0, ExcIndexRange (component, 0, 1));

    const double sine_term = std::sin(16*numbers::PI*std::sqrt(p.square()));
    const double weight    = std::exp(-5*p.square()) / std::exp(-5.);
    return sine_term * weight;
  }



  template <int dim>
  void
  BoundaryValues<dim>::value_list (const std::vector<Point<dim> > &points,
                                   std::vector<double>            &values,
                                   const unsigned int              component) const
  {
    Assert (values.size() == points.size(),
            ExcDimensionMismatch (values.size(), points.size()));

    for (unsigned int i=0; i<points.size(); ++i)
      values[i] = BoundaryValues<dim>::value (points[i], component);
  }



  // @sect3{GradientEstimation class declaration}

  // Now, finally, here comes the class that will compute the difference
  // approximation of the gradient on each cell and weighs that with a power
  // of the mesh size, as described in the introduction.  This class is a
  // simple version of the <code>DerivativeApproximation</code> class in the
  // library, that uses similar techniques to obtain finite difference
  // approximations of the gradient of a finite element field, or if higher
  // derivatives.
  //
  // The class has one public static function <code>estimate</code> that is
  // called to compute a vector of error indicators, and one private function
  // that does the actual work on an interval of all active cells. The latter
  // is called by the first one in order to be able to do the computations in
  // parallel if your computer has more than one processor. While the first
  // function accepts as parameter a vector into which the error indicator is
  // written for each cell. This vector is passed on to the second function
  // that actually computes the error indicators on some cells, and the
  // respective elements of the vector are written. By the way, we made it
  // somewhat of a convention to use vectors of floats for error indicators
  // rather than the common vectors of doubles, as the additional accuracy is
  // not necessary for estimated values.
  //
  // In addition to these two functions, the class declares to exceptions
  // which are raised when a cell has no neighbors in each of the space
  // directions (in which case the matrix described in the introduction would
  // be singular and can't be inverted), while the other one is used in the
  // more common case of invalid parameters to a function, namely a vector of
  // wrong size.
  //
  // Two annotations to this class are still in order: the first is that the
  // class has no non-static member functions or variables, so this is not
  // really a class, but rather serves the purpose of a <code>namespace</code>
  // in C++. The reason that we chose a class over a namespace is that this
  // way we can declare functions that are private, i.e. visible to the
  // outside world but not callable. This can be done with namespaces as well,
  // if one declares some functions in header files in the namespace and
  // implements these and other functions in the implementation file. The
  // functions not declared in the header file are still in the namespace but
  // are not callable from outside. However, as we have only one file here, it
  // is not possible to hide functions in the present case.
  //
  // The second is that the dimension template parameter is attached to the
  // function rather than to the class itself. This way, you don't have to
  // specify the template parameter yourself as in most other cases, but the
  // compiler can figure its value out itself from the dimension of the DoF
  // handler object that one passes as first argument.
  //
  // Finally note that the <code>IndexInterval</code> typedef is introduced as
  // a convenient abbreviation for an otherwise lengthy type name.
  class GradientEstimation
  {
  public:
    template <int dim>
    static void estimate (const DoFHandler<dim> &dof,
                          const Vector<double> &solution,
                          Vector<float>         &error_per_cell);

    DeclException2 (ExcInvalidVectorLength,
                    int, int,
                    << "Vector has length " << arg1 << ", but should have "
                    << arg2);
    DeclException0 (ExcInsufficientDirections);

  private:
    typedef std::pair<unsigned int,unsigned int> IndexInterval;

    template <int dim>
    static void estimate_interval (const DoFHandler<dim> &dof,
                                   const Vector<double> &solution,
                                   const IndexInterval   &index_interval,
                                   Vector<float>         &error_per_cell);
  };



  // @sect3{AdvectionProblem class implementation}


  // Now for the implementation of the main class. Constructor, destructor and
  // the function <code>setup_system</code> follow the same pattern that was
  // used previously, so we need not comment on these three function:
  template <int dim>
  AdvectionProblem<dim>::AdvectionProblem () :
    dof_handler (triangulation),
    fe(1)
  {}



  template <int dim>
  AdvectionProblem<dim>::~AdvectionProblem ()
  {
    dof_handler.clear ();
  }



  template <int dim>
  void AdvectionProblem<dim>::setup_system ()
  {
    dof_handler.distribute_dofs (fe);

    hanging_node_constraints.clear ();
    DoFTools::make_hanging_node_constraints (dof_handler,
                                             hanging_node_constraints);
    hanging_node_constraints.close ();

    sparsity_pattern.reinit (dof_handler.n_dofs(),
                             dof_handler.n_dofs(),
                             dof_handler.max_couplings_between_dofs());
    DoFTools::make_sparsity_pattern (dof_handler, sparsity_pattern);

    hanging_node_constraints.condense (sparsity_pattern);

    sparsity_pattern.compress();

    system_matrix.reinit (sparsity_pattern);

    solution.reinit (dof_handler.n_dofs());
    system_rhs.reinit (dof_handler.n_dofs());
  }



  // In the following function, the matrix and right hand side are
  // assembled. As stated in the documentation of the main class above, it
  // does not do this itself, but rather delegates to the function following
  // next, by splitting up the range of cells into chunks of approximately the
  // same size and assembling on each of these chunks in parallel.
  template <int dim>
  void AdvectionProblem<dim>::assemble_system ()
  {
    // First, we want to find out how many threads shall assemble the matrix
    // in parallel. A reasonable choice would be that each processor in your
    // system processes one chunk of cells; if we were to use this
    // information, we could use the value of the global variable
    // <code>multithread_info.n_cpus</code>, which is determined at start-up
    // time of your program automatically. (Note that if the library was not
    // configured for multi-threading, then the number of CPUs is set to one.)
    // However, sometimes there might be reasons to use another value. For
    // example, you might want to use less processors than there are in your
    // system in order not to use too many computational ressources. On the
    // other hand, if there are several jobs running on a computer and you
    // want to get a higher percentage of CPU time, it might be worth to start
    // more threads than there are CPUs, as most operating systems assign
    // roughly the same CPU ressources to all threads presently running. For
    // this reason, the <code>MultithreadInfo</code> class contains a
    // read-write variable <code>n_default_threads</code> which is set to
    // <code>n_cpus</code> by default, but can be set to another value. This
    // variable is also queried by functions inside the library to determine
    // how many threads they shall create.
    const unsigned int n_threads = multithread_info.n_default_threads;
    // It is worth noting, however, that this setup determines the load
    // distribution onto processor in a static way: it does not take into
    // account that some other part of our program may also be running
    // something in parallel at the same time as we get here (this is not the
    // case in the current program, but may easily be the case in more complex
    // applications). A discussion of how to deal with this case can be found
    // in the @ref threads module.
    //
    // Next, we need an object which is capable of keeping track of the
    // threads we created, and allows us to wait until they all have finished
    // (to <code>join</code> them in the language of threads). The
    // Threads::ThreadGroup class does this, which is basically just a
    // container for objects of type Threads::Thread that represent a single
    // thread; Threads::Thread is what the Threads::new_thread function below
    // will return when we start a new thread.
    //
    // Note that both Threads::ThreadGroup and Threads::Thread have a template
    // argument that represents the return type of the function being called
    // on a separate thread. Since most of the functions that we will call on
    // different threads have return type <code>void</code>, the template
    // argument has a default value <code>void</code>, so that in that case it
    // can be omitted. (However, you still need to write the angle brackets,
    // even if they are empty.)
    //
    // If you did not configure for multi-threading, then the
    // <code>new_thread</code> function that is supposed to start a new thread
    // in parallel only executes the function which should be run in parallel,
    // waits for it to return (i.e. the function is executed sequentially),
    // and puts the return value into the <code>Thread</code>
    // object. Likewise, the function <code>join</code> that is supposed to
    // wait for all spawned threads to return, returns immediately, as there
    // can't be any threads running.
    Threads::ThreadGroup<> threads;

    // Now we have to split the range of cells into chunks of approximately
    // the same size. Each thread will then assemble the local contributions
    // of the cells within its chunk and transfer these contributions to the
    // global matrix. As splitting a range of cells is a rather common task
    // when using multi-threading, there is a function in the
    // <code>Threads</code> namespace that does exactly this. In fact, it does
    // this not only for a range of cell iterators, but for iterators in
    // general, so you could use it for <code>std::vector::iterator</code> or
    // usual pointers as well.
    //
    // The function returns a vector of pairs of iterators, where the first
    // denotes the first cell of each chunk, while the second denotes the one
    // past the last (this half-open interval is the usual convention in the
    // C++ standard library, so we keep to it). Note that we have to specify
    // the actual data type of the iterators in angle brackets to the
    // function. This is necessary, since it is a template function which
    // takes the data type of the iterators as template argument; in the
    // present case, however, the data types of the two first parameters
    // differ (<code>begin_active</code> returns an
    // <code>active_iterator</code>, while <code>end</code> returns a
    // <code>raw_iterator</code>), and in this case the C++ language requires
    // us to specify the template type explicitely. For brevity, we first
    // typedef this data type to an alias.
    typedef typename DoFHandler<dim>::active_cell_iterator active_cell_iterator;
    std::vector<std::pair<active_cell_iterator,active_cell_iterator> >
    thread_ranges
      = Threads::split_range<active_cell_iterator> (dof_handler.begin_active (),
                                                    dof_handler.end (),
                                                    n_threads);

    // Finally, for each of the chunks of iterators we have computed, start
    // one thread (or if not in multi-thread mode: execute assembly on these
    // chunks sequentially). This is done using the following sequence of
    // function calls:
    for (unsigned int thread=0; thread<n_threads; ++thread)
      threads += Threads::new_thread (&AdvectionProblem<dim>::assemble_system_interval,
                                      *this,
                                      thread_ranges[thread].first,
                                      thread_ranges[thread].second);
    // The reasons and internal workings of these functions can be found in
    // the report on the subject of multi-threading, which is available online
    // as well. Suffice it to say that we create a new thread that calls the
    // <code>assemble_system_interval</code> function on the present object
    // (the <code>this</code> pointer), with the arguments following in the
    // second set of parentheses passed as parameters. The Threads::new_thread
    // function returns an object of type Threads::Thread, which we put into
    // the <code>threads</code> container. If a thread exits, the return value
    // of the function being called is put into a place such that the thread
    // objects can access it using their <code>return_value</code> function;
    // since the function we call doesn't have a return value, this does not
    // apply here. Note that you can copy around thread objects freely, and
    // that of course they will still represent the same thread.

    // When all the threads are running, the only thing we have to do is wait
    // for them to finish. This is necessary of course, as we can't proceed
    // with our tasks before the matrix and right hand side are
    // assemblesd. Waiting for all the threads to finish can be done using the
    // <code>joint_all</code> function in the <code>ThreadGroup</code>
    // container, which just calls <code>join</code> on each of the thread
    // objects it stores.
    //
    // Again, if the library was not configured to use multi-threading, then
    // no threads can run in parallel and the function returns immediately.
    threads.join_all ();


    // After the matrix has been assembled in parallel, we stil have to
    // eliminate hanging node constraints. This is something that can't be
    // done on each of the threads separately, so we have to do it now.
    hanging_node_constraints.condense (system_matrix);
    hanging_node_constraints.condense (system_rhs);
    // Note also, that unlike in previous examples, there are no boundary
    // conditions to be applied to the system of equations. This, of course,
    // is due to the fact that we have included them into the weak formulation
    // of the problem.
  }



  // Now, this is the function that does the actual work. It is not very
  // different from the <code>assemble_system</code> functions of previous
  // example programs, so we will again only comment on the differences. The
  // mathematical stuff follows closely what we have said in the introduction.
  template <int dim>
  void
  AdvectionProblem<dim>::
  assemble_system_interval (const typename DoFHandler<dim>::active_cell_iterator &begin,
                            const typename DoFHandler<dim>::active_cell_iterator &end)
  {
    // First of all, we will need some objects that describe boundary values,
    // right hand side function and the advection field. As we will only
    // perform actions on these objects that do not change them, we declare
    // them as constant, which can enable the compiler in some cases to
    // perform additional optimizations.
    const AdvectionField<dim> advection_field;
    const RightHandSide<dim>  right_hand_side;
    const BoundaryValues<dim> boundary_values;

    // Next we need quadrature formula for the cell terms, but also for the
    // integral over the inflow boundary, which will be a face integral. As we
    // use bilinear elements, Gauss formulae with two points in each space
    // direction are sufficient.
    QGauss<dim>   quadrature_formula(2);
    QGauss<dim-1> face_quadrature_formula(2);

    // Finally, we need objects of type <code>FEValues</code> and
    // <code>FEFaceValues</code>. For the cell terms we need the values and
    // gradients of the shape functions, the quadrature points in order to
    // determine the source density and the advection field at a given point,
    // and the weights of the quadrature points times the determinant of the
    // Jacobian at these points. In contrast, for the boundary integrals, we
    // don't need the gradients, but rather the normal vectors to the cells.
    FEValues<dim> fe_values (fe, quadrature_formula,
                             update_values   | update_gradients |
                             update_quadrature_points | update_JxW_values);
    FEFaceValues<dim> fe_face_values (fe, face_quadrature_formula,
                                      update_values     | update_quadrature_points   |
                                      update_JxW_values | update_normal_vectors);

    // Then we define some abbreviations to avoid unnecessarily long lines:
    const unsigned int   dofs_per_cell   = fe.dofs_per_cell;
    const unsigned int   n_q_points      = quadrature_formula.size();
    const unsigned int   n_face_q_points = face_quadrature_formula.size();

    // We declare cell matrix and cell right hand side...
    FullMatrix<double>   cell_matrix (dofs_per_cell, dofs_per_cell);
    Vector<double>       cell_rhs (dofs_per_cell);

    // ... an array to hold the global indices of the degrees of freedom of
    // the cell on which we are presently working...
    std::vector<unsigned int> local_dof_indices (dofs_per_cell);

    // ... and array in which the values of right hand side, advection
    // direction, and boundary values will be stored, for cell and face
    // integrals respectively:
    std::vector<double>         rhs_values (n_q_points);
    std::vector<Tensor<1,dim> > advection_directions (n_q_points);
    std::vector<double>         face_boundary_values (n_face_q_points);
    std::vector<Tensor<1,dim> > face_advection_directions (n_face_q_points);

    // Then we start the main loop over the cells:
    typename DoFHandler<dim>::active_cell_iterator cell;
    for (cell=begin; cell!=end; ++cell)
      {
        // First clear old contents of the cell contributions...
        cell_matrix = 0;
        cell_rhs = 0;

        // ... then initialize the <code>FEValues</code> object...
        fe_values.reinit (cell);

        // ... obtain the values of right hand side and advection directions
        // at the quadrature points...
        advection_field.value_list (fe_values.get_quadrature_points(),
                                    advection_directions);
        right_hand_side.value_list (fe_values.get_quadrature_points(),
                                    rhs_values);

        // ... set the value of the streamline diffusion parameter as
        // described in the introduction...
        const double delta = 0.1 * cell->diameter ();

        // ... and assemble the local contributions to the system matrix and
        // right hand side as also discussed above:
        for (unsigned int q_point=0; q_point<n_q_points; ++q_point)
          for (unsigned int i=0; i<dofs_per_cell; ++i)
            {
              for (unsigned int j=0; j<dofs_per_cell; ++j)
                cell_matrix(i,j) += ((advection_directions[q_point] *
                                      fe_values.shape_grad(j,q_point)   *
                                      (fe_values.shape_value(i,q_point) +
                                       delta *
                                       (advection_directions[q_point] *
                                        fe_values.shape_grad(i,q_point)))) *
                                     fe_values.JxW(q_point));

              cell_rhs(i) += ((fe_values.shape_value(i,q_point) +
                               delta *
                               (advection_directions[q_point] *
                                fe_values.shape_grad(i,q_point))        ) *
                              rhs_values[q_point] *
                              fe_values.JxW (q_point));
            };

        // Besides the cell terms which we have build up now, the bilinear
        // form of the present problem also contains terms on the boundary of
        // the domain. Therefore, we have to check whether any of the faces of
        // this cell are on the boundary of the domain, and if so assemble the
        // contributions of this face as well. Of course, the bilinear form
        // only contains contributions from the <code>inflow</code> part of
        // the boundary, but to find out whether a certain part of a face of
        // the present cell is part of the inflow boundary, we have to have
        // information on the exact location of the quadrature points and on
        // the direction of flow at this point; we obtain this information
        // using the FEFaceValues object and only decide within the main loop
        // whether a quadrature point is on the inflow boundary.
        for (unsigned int face=0; face<GeometryInfo<dim>::faces_per_cell; ++face)
          if (cell->face(face)->at_boundary())
            {
              // Ok, this face of the present cell is on the boundary of the
              // domain. Just as for the usual FEValues object which we have
              // used in previous examples and also above, we have to
              // reinitialize the FEFaceValues object for the present face:
              fe_face_values.reinit (cell, face);

              // For the quadrature points at hand, we ask for the values of
              // the inflow function and for the direction of flow:
              boundary_values.value_list (fe_face_values.get_quadrature_points(),
                                          face_boundary_values);
              advection_field.value_list (fe_face_values.get_quadrature_points(),
                                          face_advection_directions);

              // Now loop over all quadrature points and see whether it is on
              // the inflow or outflow part of the boundary. This is
              // determined by a test whether the advection direction points
              // inwards or outwards of the domain (note that the normal
              // vector points outwards of the cell, and since the cell is at
              // the boundary, the normal vector points outward of the domain,
              // so if the advection direction points into the domain, its
              // scalar product with the normal vector must be negative):
              for (unsigned int q_point=0; q_point<n_face_q_points; ++q_point)
                if (fe_face_values.normal_vector(q_point) *
                    face_advection_directions[q_point]
                    < 0)
                  // If the is part of the inflow boundary, then compute the
                  // contributions of this face to the global matrix and right
                  // hand side, using the values obtained from the
                  // FEFaceValues object and the formulae discussed in the
                  // introduction:
                  for (unsigned int i=0; i<dofs_per_cell; ++i)
                    {
                      for (unsigned int j=0; j<dofs_per_cell; ++j)
                        cell_matrix(i,j) -= (face_advection_directions[q_point] *
                                             fe_face_values.normal_vector(q_point) *
                                             fe_face_values.shape_value(i,q_point) *
                                             fe_face_values.shape_value(j,q_point) *
                                             fe_face_values.JxW(q_point));

                      cell_rhs(i) -= (face_advection_directions[q_point] *
                                      fe_face_values.normal_vector(q_point) *
                                      face_boundary_values[q_point]         *
                                      fe_face_values.shape_value(i,q_point) *
                                      fe_face_values.JxW(q_point));
                    };
            };


        // Now go on by transferring the local contributions to the system of
        // equations into the global objects. The first step was to obtain the
        // global indices of the degrees of freedom on this cell.
        cell->get_dof_indices (local_dof_indices);

        // Up until now we have not taken care of the fact that this function
        // might run more than once in parallel, as the operations above only
        // work on variables that are local to this function, or if they are
        // global (such as the information on the grid, the DoF handler, or
        // the DoF numbers) they are only read. Thus, the different threads do
        // not disturb each other.
        //
        // On the other hand, we would now like to write the local
        // contributions to the global system of equations into the global
        // objects. This needs some kind of synchronisation, as if we would
        // not take care of the fact that multiple threads write into the
        // matrix at the same time, we might be surprised that one threads
        // reads data from the matrix that another thread is presently
        // overwriting, or similar things. Thus, to make sure that only one
        // thread operates on these objects at a time, we have to lock
        // it. This is done using a <code>Mutex</code>, which is short for
        // <code>mutually exclusive</code>: a thread that wants to write to
        // the global objects acquires this lock, but has to wait if it is
        // presently owned by another thread. If it has acquired the lock, it
        // can be sure that no other thread is presently writing to the
        // matrix, and can do so freely. When finished, we release the lock
        // again so as to allow other threads to acquire it and write to the
        // matrix.
        assembler_lock.acquire ();
        for (unsigned int i=0; i<dofs_per_cell; ++i)
          {
            for (unsigned int j=0; j<dofs_per_cell; ++j)
              system_matrix.add (local_dof_indices[i],
                                 local_dof_indices[j],
                                 cell_matrix(i,j));

            system_rhs(local_dof_indices[i]) += cell_rhs(i);
          };
        assembler_lock.release ();
        // At this point, the locked operations on the global matrix are done,
        // i.e. other threads can now enter into the protected section by
        // acquiring the lock. Two final notes are in place here, however:
        //
        // 1. If the library was not configured for multi-threading, then
        // there can't be parallel threads and there is no need to
        // synchronize. Thus, the <code>lock</code> and <code>release</code>
        // functions are no-ops, i.e. they return without doing anything.
        //
        // 2. In order to work properly, it is essential that all threads try
        // to acquire the same lock. This, of course, can not be achieved if
        // the lock is a local variable, as then each thread would acquire its
        // own lock. Therefore, the lock variable is a member variable of the
        // class; since all threads execute member functions of the same
        // object, they have the same <code>this</code> pointer and therefore
        // also operate on the same <code>lock</code>.
      };
  }



  // Following is the function that solves the linear system of equations. As
  // the system is no more symmetric positive definite as in all the previous
  // examples, we can't use the Conjugate Gradients method anymore. Rather, we
  // use a solver that is tailored to nonsymmetric systems like the one at
  // hand, the BiCGStab method. As preconditioner, we use the Jacobi method.
  template <int dim>
  void AdvectionProblem<dim>::solve ()
  {
    SolverControl           solver_control (1000, 1e-12);
    SolverBicgstab<>        bicgstab (solver_control);

    PreconditionJacobi<> preconditioner;
    preconditioner.initialize(system_matrix, 1.0);

    bicgstab.solve (system_matrix, solution, system_rhs,
                    preconditioner);

    hanging_node_constraints.distribute (solution);
  }


  // The following function refines the grid according to the quantity
  // described in the introduction. The respective computations are made in
  // the class <code>GradientEstimation</code>. The only difference to
  // previous examples is that we refine a little more aggressively (0.5
  // instead of 0.3 of the number of cells).
  template <int dim>
  void AdvectionProblem<dim>::refine_grid ()
  {
    Vector<float> estimated_error_per_cell (triangulation.n_active_cells());

    GradientEstimation::estimate (dof_handler,
                                  solution,
                                  estimated_error_per_cell);

    GridRefinement::refine_and_coarsen_fixed_number (triangulation,
                                                     estimated_error_per_cell,
                                                     0.5, 0.03);

    triangulation.execute_coarsening_and_refinement ();
  }



  // Writing output to disk is done in the same way as in the previous
  // examples...
  template <int dim>
  void AdvectionProblem<dim>::output_results (const unsigned int cycle) const
  {
    std::string filename = "grid-";
    filename += ('0' + cycle);
    Assert (cycle < 10, ExcInternalError());

    filename += ".eps";
    std::ofstream output (filename.c_str());

    GridOut grid_out;
    grid_out.write_eps (triangulation, output);
  }


  // ... as is the main loop (setup -- solve -- refine)
  template <int dim>
  void AdvectionProblem<dim>::run ()
  {
    for (unsigned int cycle=0; cycle<6; ++cycle)
      {
        std::cout << "Cycle " << cycle << ':' << std::endl;

        if (cycle == 0)
          {
            GridGenerator::hyper_cube (triangulation, -1, 1);
            triangulation.refine_global (4);
          }
        else
          {
            refine_grid ();
          };


        std::cout << "   Number of active cells:       "
                  << triangulation.n_active_cells()
                  << std::endl;

        setup_system ();

        std::cout << "   Number of degrees of freedom: "
                  << dof_handler.n_dofs()
                  << std::endl;

        assemble_system ();
        solve ();
        output_results (cycle);
      };

    DataOut<dim> data_out;
    data_out.attach_dof_handler (dof_handler);
    data_out.add_data_vector (solution, "solution");
    data_out.build_patches ();

    std::ofstream output ("final-solution.gmv");
    data_out.write_gmv (output);
  }



  // @sect3{GradientEstimation class implementation}

  // Now for the implementation of the <code>GradientEstimation</code>
  // class. The first function does not much except for delegating work to the
  // other function:
  template <int dim>
  void
  GradientEstimation::estimate (const DoFHandler<dim> &dof_handler,
                                const Vector<double> &solution,
                                Vector<float>         &error_per_cell)
  {
    // Before starting with the work, we check that the vector into which the
    // results are written, has the right size. It is a common error that such
    // parameters have the wrong size, but the resulting damage by not
    // catching these errors are very subtle as they are usually corruption of
    // data somewhere in memory. Often, the problems emerging from this are
    // not reproducible, and we found that it is well worth the effort to
    // check for such things.
    Assert (error_per_cell.size() == dof_handler.get_tria().n_active_cells(),
            ExcInvalidVectorLength (error_per_cell.size(),
                                    dof_handler.get_tria().n_active_cells()));

    // Next, we subdivide the range of cells into chunks of equal size. Just
    // as we have used the function <code>Threads::split_range</code> when
    // assembling above, there is a function that computes intervals of
    // roughly equal size from a larger interval. This is used here:
    const unsigned int n_threads = multithread_info.n_default_threads;
    std::vector<IndexInterval> index_intervals
      = Threads::split_interval (0, dof_handler.get_tria().n_active_cells(),
                                 n_threads);

    // In the same way as before, we use a <code>Threads::ThreadGroup</code>
    // object to collect the descriptor objects of different threads. Note
    // that as the function called is not a member function, but rather a
    // static function, we need not (and can not) pass a <code>this</code>
    // pointer to the <code>new_thread</code> function in this case.
    //
    // Taking pointers to templated functions seems to be notoriously
    // difficult for many compilers (since there are several functions with
    // the same name -- just as with overloaded functions). It therefore
    // happens quite frequently that we can't directly insert taking the
    // address of a function in the call to <code>encapsulate</code> for one
    // or the other compiler, but have to take a temporary variable for that
    // purpose. Here, in this case, Compaq's <code>cxx</code> compiler choked
    // on the code so we use this workaround with the function pointer:
    Threads::ThreadGroup<> threads;
    void (*estimate_interval_ptr) (const DoFHandler<dim> &,
                                   const Vector<double> &,
                                   const IndexInterval &,
                                   Vector<float> &)
      = &GradientEstimation::template estimate_interval<dim>;
    for (unsigned int i=0; i<n_threads; ++i)
      threads += Threads::new_thread (estimate_interval_ptr,
                                      dof_handler, solution,
                                      index_intervals[i],
                                      error_per_cell);
    // Ok, now the threads are at work, and we only have to wait for them to
    // finish their work:
    threads.join_all ();
    // Note that if the value of the variable
    // <code>multithread_info.n_default_threads</code> was one, or if the
    // library was not configured to use threads, then the sequence of
    // commands above reduced to a complicated way to simply call the
    // <code>estimate_interval</code> function with the whole range of cells
    // to work on. However, using the way above, we are able to write the
    // program such that it makes no difference whether we presently work with
    // multiple threads or in single-threaded mode, thus eliminating the need
    // to write code included in conditional preprocessor sections.
  }


  // Following now the function that actually computes the finite difference
  // approximation to the gradient. The general outline of the function is to
  // loop over all the cells in the range of iterators designated by the third
  // argument, and on each cell first compute the list of active neighbors of
  // the present cell and then compute the quantities described in the
  // introduction for each of the neighbors. The reason for this order is that
  // it is not a one-liner to find a given neighbor with locally refined
  // meshes. In principle, an optimized implementation would find neighbors
  // and the quantities depending on them in one step, rather than first
  // building a list of neighbors and in a second step their contributions.
  //
  // Now for the details:
  template <int dim>
  void
  GradientEstimation::estimate_interval (const DoFHandler<dim> &dof_handler,
                                         const Vector<double> &solution,
                                         const IndexInterval   &index_interval,
                                         Vector<float>         &error_per_cell)
  {
    // First we need a way to extract the values of the given finite element
    // function at the center of the cells. As usual with values of finite
    // element functions, we use an object of type <code>FEValues</code>, and
    // we use (or mis-use in this case) the midpoint quadrature rule to get at
    // the values at the center. Note that the <code>FEValues</code> object
    // only needs to compute the values at the centers, and the location of
    // the quadrature points in real space in order to get at the vectors
    // <code>y</code>.
    QMidpoint<dim> midpoint_rule;
    FEValues<dim>  fe_midpoint_value (dof_handler.get_fe(),
                                      midpoint_rule,
                                      update_values | update_quadrature_points);

    // Then we need space foe the tensor <code>Y</code>, which is the sum of
    // outer products of the y-vectors.
    Tensor<2,dim> Y;

    // Then define iterators into the cells and into the output vector, which
    // are to be looped over by the present instance of this function. We get
    // start and end iterators over cells by setting them to the first active
    // cell and advancing them using the given start and end index. Note that
    // we can use the <code>advance</code> function of the standard C++
    // library, but that we have to cast the distance by which the iterator is
    // to be moved forward to a signed quantity in order to avoid warnings by
    // the compiler.
    typename DoFHandler<dim>::active_cell_iterator cell, endc;

    cell = dof_handler.begin_active();
    advance (cell, static_cast<signed int>(index_interval.first));

    endc = dof_handler.begin_active();
    advance (endc, static_cast<signed int>(index_interval.second));

    // Getting an iterator into the output array is simpler. We don't need an
    // end iterator, as we always move this iterator forward by one element
    // for each cell we are on, but stop the loop when we hit the end cell, so
    // we need not have an end element for this iterator.
    Vector<float>::iterator
    error_on_this_cell = error_per_cell.begin() + index_interval.first;


    // Then we allocate a vector to hold iterators to all active neighbors of
    // a cell. We reserve the maximal number of active neighbors in order to
    // avoid later reallocations. Note how this maximal number of active
    // neighbors is computed here.
    std::vector<typename DoFHandler<dim>::active_cell_iterator> active_neighbors;
    active_neighbors.reserve (GeometryInfo<dim>::faces_per_cell *
                              GeometryInfo<dim>::max_children_per_face);

    // Well then, after all these preliminaries, lets start the computations:
    for (; cell!=endc; ++cell, ++error_on_this_cell)
      {
        // First initialize the <code>FEValues</code> object, as well as the
        // <code>Y</code> tensor:
        fe_midpoint_value.reinit (cell);
        Y.clear ();

        // Then allocate the vector that will be the sum over the y-vectors
        // times the approximate directional derivative:
        Tensor<1,dim> projected_gradient;


        // Now before going on first compute a list of all active neighbors of
        // the present cell. We do so by first looping over all faces and see
        // whether the neighbor there is active, which would be the case if it
        // is on the same level as the present cell or one level coarser (note
        // that a neighbor can only be once coarser than the present cell, as
        // we only allow a maximal difference of one refinement over a face in
        // deal.II). Alternatively, the neighbor could be on the same level
        // and be further refined; then we have to find which of its children
        // are next to the present cell and select these (note that if a child
        // of of neighbor of an active cell that is next to this active cell,
        // needs necessarily be active itself, due to the one-refinement rule
        // cited above).
        //
        // Things are slightly different in one space dimension, as there the
        // one-refinement rule does not exist: neighboring active cells may
        // differ in as many refinement levels as they like. In this case, the
        // computation becomes a little more difficult, but we will explain
        // this below.
        //
        // Before starting the loop over all neighbors of the present cell, we
        // have to clear the array storing the iterators to the active
        // neighbors, of course.
        active_neighbors.clear ();
        for (unsigned int face_no=0; face_no<GeometryInfo<dim>::faces_per_cell; ++face_no)
          if (! cell->at_boundary(face_no))
            {
              // First define an abbreviation for the iterator to the face and
              // the neighbor
              const typename DoFHandler<dim>::face_iterator
              face = cell->face(face_no);
              const typename DoFHandler<dim>::cell_iterator
              neighbor = cell->neighbor(face_no);

              // Then check whether the neighbor is active. If it is, then it
              // is on the same level or one level coarser (if we are not in
              // 1D), and we are interested in it in any case.
              if (neighbor->active())
                active_neighbors.push_back (neighbor);
              else
                {
                  // If the neighbor is not active, then check its children.
                  if (dim == 1)
                    {
                      // To find the child of the neighbor which bounds to the
                      // present cell, successively go to its right child if
                      // we are left of the present cell (n==0), or go to the
                      // left child if we are on the right (n==1), until we
                      // find an active cell.
                      typename DoFHandler<dim>::cell_iterator
                      neighbor_child = neighbor;
                      while (neighbor_child->has_children())
                        neighbor_child = neighbor_child->child (face_no==0 ? 1 : 0);

                      // As this used some non-trivial geometrical intuition,
                      // we might want to check whether we did it right,
                      // i.e. check whether the neighbor of the cell we found
                      // is indeed the cell we are presently working
                      // on. Checks like this are often useful and have
                      // frequently uncovered errors both in algorithms like
                      // the line above (where it is simple to involuntarily
                      // exchange <code>n==1</code> for <code>n==0</code> or
                      // the like) and in the library (the assumptions
                      // underlying the algorithm above could either be wrong,
                      // wrongly documented, or are violated due to an error
                      // in the library). One could in principle remove such
                      // checks after the program works for some time, but it
                      // might be a good things to leave it in anyway to check
                      // for changes in the library or in the algorithm above.
                      //
                      // Note that if this check fails, then this is certainly
                      // an error that is irrecoverable and probably qualifies
                      // as an internal error. We therefore use a predefined
                      // exception class to throw here.
                      Assert (neighbor_child->neighbor(face_no==0 ? 1 : 0)==cell,
                              ExcInternalError());

                      // If the check succeeded, we push the active neighbor
                      // we just found to the stack we keep:
                      active_neighbors.push_back (neighbor_child);
                    }
                  else
                    // If we are not in 1d, we collect all neighbor children
                    // `behind' the subfaces of the current face
                    for (unsigned int subface_no=0; subface_no<face->n_children(); ++subface_no)
                      active_neighbors.push_back (
                        cell->neighbor_child_on_subface(face_no, subface_no));
                };
            };

        // OK, now that we have all the neighbors, lets start the computation
        // on each of them. First we do some preliminaries: find out about the
        // center of the present cell and the solution at this point. The
        // latter is obtained as a vector of function values at the quadrature
        // points, of which there are only one, of course. Likewise, the
        // position of the center is the position of the first (and only)
        // quadrature point in real space.
        const Point<dim> this_center = fe_midpoint_value.quadrature_point(0);

        std::vector<double> this_midpoint_value(1);
        fe_midpoint_value.get_function_values (solution, this_midpoint_value);


        // Now loop over all active neighbors and collect the data we
        // need. Allocate a vector just like <code>this_midpoint_value</code>
        // which we will use to store the value of the solution in the
        // midpoint of the neighbor cell. We allocate it here already, since
        // that way we don't have to allocate memory repeatedly in each
        // iteration of this inner loop (memory allocation is a rather
        // expensive operation):
        std::vector<double> neighbor_midpoint_value(1);
        typename std::vector<typename DoFHandler<dim>::active_cell_iterator>::const_iterator
        neighbor_ptr = active_neighbors.begin();
        for (; neighbor_ptr!=active_neighbors.end(); ++neighbor_ptr)
          {
            // First define an abbreviation for the iterator to the active
            // neighbor cell:
            const typename DoFHandler<dim>::active_cell_iterator
            neighbor = *neighbor_ptr;

            // Then get the center of the neighbor cell and the value of the
            // finite element function thereon. Note that for this information
            // we have to reinitialize the <code>FEValues</code> object for
            // the neighbor cell.
            fe_midpoint_value.reinit (neighbor);
            const Point<dim> neighbor_center = fe_midpoint_value.quadrature_point(0);

            fe_midpoint_value.get_function_values (solution,
                                                   neighbor_midpoint_value);

            // Compute the vector <code>y</code> connecting the centers of the
            // two cells. Note that as opposed to the introduction, we denote
            // by <code>y</code> the normalized difference vector, as this is
            // the quantity used everywhere in the computations.
            Point<dim>   y        = neighbor_center - this_center;
            const double distance = std::sqrt(y.square());
            y /= distance;

            // Then add up the contribution of this cell to the Y matrix...
            for (unsigned int i=0; i<dim; ++i)
              for (unsigned int j=0; j<dim; ++j)
                Y[i][j] += y[i] * y[j];

            // ... and update the sum of difference quotients:
            projected_gradient += (neighbor_midpoint_value[0] -
                                   this_midpoint_value[0]) /
                                  distance *
                                  y;
          };

        // If now, after collecting all the information from the neighbors, we
        // can determine an approximation of the gradient for the present
        // cell, then we need to have passed over vectors <code>y</code> which
        // span the whole space, otherwise we would not have all components of
        // the gradient. This is indicated by the invertability of the matrix.
        //
        // If the matrix should not be invertible, this means that the present
        // cell had an insufficient number of active neighbors. In contrast to
        // all previous cases, where we raised exceptions, this is, however,
        // not a programming error: it is a runtime error that can happen in
        // optimized mode even if it ran well in debug mode, so it is
        // reasonable to try to catch this error also in optimized mode. For
        // this case, there is the <code>AssertThrow</code> macro: it checks
        // the condition like the <code>Assert</code> macro, but not only in
        // debug mode; it then outputs an error message, but instead of
        // terminating the program as in the case of the <code>Assert</code>
        // macro, the exception is thrown using the <code>throw</code> command
        // of C++. This way, one has the possibility to catch this error and
        // take reasonable counter actions. One such measure would be to
        // refine the grid globally, as the case of insufficient directions
        // can not occur if every cell of the initial grid has been refined at
        // least once.
        AssertThrow (determinant(Y) != 0,
                     ExcInsufficientDirections());

        // If, on the other hand the matrix is invertible, then invert it,
        // multiply the other quantity with it and compute the estimated error
        // using this quantity and the right powers of the mesh width:
        const Tensor<2,dim> Y_inverse = invert(Y);

        Point<dim> gradient;
        contract (gradient, Y_inverse, projected_gradient);

        *error_on_this_cell = (std::pow(cell->diameter(),
                                        1+1.0*dim/2) *
                               std::sqrt(gradient.square()));
      };
  }
}


// @sect3{Main function}

// The <code>main</code> function is exactly like in previous examples, with
// the only difference in the name of the main class that actually does the
// computation.
int main ()
{
  try
    {
      dealii::deallog.depth_console (0);

      Step9::AdvectionProblem<2> advection_problem_2d;
      advection_problem_2d.run ();
    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Exception on processing: " << std::endl
                << exc.what() << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }
  catch (...)
    {
      std::cerr << std::endl << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Unknown exception!" << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    };

  return 0;
}
