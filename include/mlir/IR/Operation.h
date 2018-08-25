//===- Operation.h - MLIR Operation Class -----------------------*- C++ -*-===//
//
// Copyright 2019 The MLIR Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================

#ifndef MLIR_IR_OPERATION_H
#define MLIR_IR_OPERATION_H

#include "mlir/IR/Identifier.h"
#include "mlir/Support/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"

namespace mlir {
class Attribute;
class AttributeListStorage;
class AbstractOperation;
template <typename OpType> class ConstOpPointer;
template <typename OpType> class OpPointer;
template <typename ObjectType, typename ElementType> class OperandIterator;
template <typename ObjectType, typename ElementType> class ResultIterator;
class Function;
class SSAValue;
class Type;

/// NamedAttribute is a used for operation attribute lists, it holds an
/// identifier for the name and a value for the attribute.  The attribute
/// pointer should always be non-null.
typedef std::pair<Identifier, Attribute*> NamedAttribute;

/// This represents an operation in an abstracted form, suitable for use with
/// the builder APIs.  This object is a large and heavy weight object meant to
/// be used as a temporary object on the stack.  It is generally unwise to put
/// this in a collection.
struct OperationState {
  MLIRContext *const context;
  Attribute *location;
  Identifier name;
  SmallVector<SSAValue *, 4> operands;
  /// Types of the results of this operation.
  SmallVector<Type *, 4> types;
  SmallVector<NamedAttribute, 4> attributes;

public:
  OperationState(MLIRContext *context, Attribute *location, StringRef name)
      : context(context), location(location),
        name(Identifier::get(name, context)) {}

  OperationState(MLIRContext *context, Attribute *location, Identifier name)
      : context(context), location(location), name(name) {}

  OperationState(MLIRContext *context, Attribute *location, StringRef name,
                 ArrayRef<SSAValue *> operands, ArrayRef<Type *> types,
                 ArrayRef<NamedAttribute> attributes = {})
      : context(context), location(location),
        name(Identifier::get(name, context)),
        operands(operands.begin(), operands.end()),
        types(types.begin(), types.end()),
        attributes(attributes.begin(), attributes.end()) {}

  void addOperands(ArrayRef<SSAValue *> newOperands) {
    operands.append(newOperands.begin(), newOperands.end());
  }

  void addTypes(ArrayRef<Type *> newTypes) {
    types.append(newTypes.begin(), newTypes.end());
  }

  void addAttribute(StringRef name, Attribute *attr) {
    attributes.push_back({Identifier::get(name, context), attr});
  }
};

/// Operations represent all of the arithmetic and other basic computation in
/// MLIR.  This class is the common implementation details behind OperationInst
/// and OperationStmt.
///
class Operation {
public:
  /// Return the context this operation is associated with.
  MLIRContext *getContext() const;

  /// The source location the operation was defined or derived from.  Note that
  /// it is possible for this pointer to be null.
  Attribute *getLoc() const;

  /// Return the function this operation is defined in.  This has a verbose
  /// name to avoid name lookup ambiguities.
  Function *getOperationFunction();

  const Function *getOperationFunction() const {
    return const_cast<Operation *>(this)->getOperationFunction();
  }

  /// The name of an operation is the key identifier for it.
  Identifier getName() const { return nameAndIsInstruction.getPointer(); }

  /// Return the number of operands this operation has.
  unsigned getNumOperands() const;

  SSAValue *getOperand(unsigned idx);
  const SSAValue *getOperand(unsigned idx) const {
    return const_cast<Operation *>(this)->getOperand(idx);
  }
  void setOperand(unsigned idx, SSAValue *value);

  // Support non-const operand iteration.
  using operand_iterator = OperandIterator<Operation, SSAValue>;
  operand_iterator operand_begin();
  operand_iterator operand_end();
  llvm::iterator_range<operand_iterator> getOperands();

  // Support const operand iteration.
  using const_operand_iterator =
      OperandIterator<const Operation, const SSAValue>;
  const_operand_iterator operand_begin() const;
  const_operand_iterator operand_end() const;
  llvm::iterator_range<const_operand_iterator> getOperands() const;

  /// Return the number of results this operation has.
  unsigned getNumResults() const;

  /// Return the indicated result.
  SSAValue *getResult(unsigned idx);
  const SSAValue *getResult(unsigned idx) const {
    return const_cast<Operation *>(this)->getResult(idx);
  }

  // Support non-const result iteration.
  using result_iterator = ResultIterator<Operation, SSAValue>;
  result_iterator result_begin();
  result_iterator result_end();
  llvm::iterator_range<result_iterator> getResults();

  // Support const operand iteration.
  using const_result_iterator = ResultIterator<const Operation, const SSAValue>;
  const_result_iterator result_begin() const;
  const_result_iterator result_end() const;
  llvm::iterator_range<const_result_iterator> getResults() const;

  // Attributes.  Operations may optionally carry a list of attributes that
  // associate constants to names.  Attributes may be dynamically added and
  // removed over the lifetime of an operation.
  //
  // We assume there will be relatively few attributes on a given operation
  // (maybe a dozen or so, but not hundreds or thousands) so we use linear
  // searches for everything.

  /// Return all of the attributes on this operation.
  ArrayRef<NamedAttribute> getAttrs() const;

  /// Return the specified attribute if present, null otherwise.
  Attribute *getAttr(Identifier name) const {
    for (auto elt : getAttrs())
      if (elt.first == name)
        return elt.second;
    return nullptr;
  }

  Attribute *getAttr(StringRef name) const {
    for (auto elt : getAttrs())
      if (elt.first.is(name))
        return elt.second;
    return nullptr;
  }

  template <typename AttrClass>
  AttrClass *getAttrOfType(Identifier name) const {
    return dyn_cast_or_null<AttrClass>(getAttr(name));
  }

  template <typename AttrClass>
  AttrClass *getAttrOfType(StringRef name) const {
    return dyn_cast_or_null<AttrClass>(getAttr(name));
  }

  /// If the an attribute exists with the specified name, change it to the new
  /// value.  Otherwise, add a new attribute with the specified name/value.
  void setAttr(Identifier name, Attribute *value);

  enum class RemoveResult {
    Removed, NotFound
  };

  /// Remove the attribute with the specified name if it exists.  The return
  /// value indicates whether the attribute was present or not.
  RemoveResult removeAttr(Identifier name);

  /// Emit an error about fatal conditions with this operation, reporting up to
  /// any diagnostic handlers that may be listening.  NOTE: This may terminate
  /// the containing application, only use when the IR is in an inconsistent
  /// state.
  void emitError(const Twine &message) const;

  /// Emit a warning about this operation, reporting up to any diagnostic
  /// handlers that may be listening.
  void emitWarning(const Twine &message) const;

  /// Emit a note about this operation, reporting up to any diagnostic
  /// handlers that may be listening.
  void emitNote(const Twine &message) const;

  /// If this operation has a registered operation description in the
  /// OperationSet, return it.  Otherwise return null.
  const AbstractOperation *getAbstractOperation() const;

  /// The getAs methods perform a dynamic cast from an Operation (like
  /// OperationInst and OperationStmt) to a typed Op like DimOp.  This returns
  /// a null OpPointer on failure.
  template <typename OpClass>
  OpPointer<OpClass> getAs() {
    bool isMatch = OpClass::isClassFor(this);
    return OpPointer<OpClass>(OpClass(isMatch ? this : nullptr));
  }

  /// The getAs methods perform a dynamic cast from an Operation (like
  /// OperationInst and OperationStmt) to a typed Op like DimOp.  This returns
  /// a null ConstOpPointer on failure.
  template <typename OpClass>
  ConstOpPointer<OpClass> getAs() const {
    bool isMatch = OpClass::isClassFor(this);
    return ConstOpPointer<OpClass>(OpClass(isMatch ? this : nullptr));
  }

  /// The is methods return true if the operation is a typed op (like DimOp) of
  /// of the given class.
  template <typename OpClass> bool is() const {
    return OpClass::isClassFor(this);
  }

  enum class OperationKind { Instruction, Statement };
  // This is used to implement the dynamic casting logic, but you shouldn't
  // call it directly.  Use something like isa<OperationInst>(someOp) instead.
  OperationKind getOperationKind() const {
    return nameAndIsInstruction.getInt() ? OperationKind::Instruction
                                         : OperationKind::Statement;
  }

protected:
  Operation(bool isInstruction, Identifier name, ArrayRef<NamedAttribute> attrs,
            MLIRContext *context);
  ~Operation();

private:
  Operation(const Operation&) = delete;
  void operator=(const Operation&) = delete;

  /// This holds the name of the operation, and a bool.  The bool is true if
  /// this operation is an OperationInst, false if it is a OperationStmt.
  llvm::PointerIntPair<Identifier, 1, bool> nameAndIsInstruction;

  /// This holds general named attributes for the operation.
  AttributeListStorage *attrs;
};

/// This is a helper template used to implement an iterator that contains a
/// pointer to some object and an index into it.  The iterator moves the
/// index but keeps the object constant.
template <typename ConcreteType, typename ObjectType, typename ElementType>
class IndexedAccessorIterator
    : public llvm::iterator_facade_base<
          ConcreteType, std::random_access_iterator_tag, ElementType *> {
public:
  ptrdiff_t operator-(const IndexedAccessorIterator &rhs) const {
    assert(object == rhs.object && "incompatible iterators");
    return index - rhs.index;
  }
  bool operator==(const IndexedAccessorIterator &rhs) const {
    return object == rhs.object && index == rhs.index;
  }
  bool operator<(const IndexedAccessorIterator &rhs) const {
    assert(object == rhs.object && "incompatible iterators");
    return index < rhs.index;
  }

  ConcreteType &operator+=(ptrdiff_t offset) {
    this->index += offset;
    return static_cast<ConcreteType &>(*this);
  }
  ConcreteType &operator-=(ptrdiff_t offset) {
    this->index -= offset;
    return static_cast<ConcreteType &>(*this);
  }

protected:
  IndexedAccessorIterator(ObjectType *object, unsigned index)
      : object(object), index(index) {}
  ObjectType *object;
  unsigned index;
};

/// This template implements the operand iterators for the various IR classes
/// in terms of getOperand(idx).
template <typename ObjectType, typename ElementType>
class OperandIterator final
    : public IndexedAccessorIterator<OperandIterator<ObjectType, ElementType>,
                                     ObjectType, ElementType> {
public:
  /// Initializes the operand iterator to the specified operand index.
  OperandIterator(ObjectType *object, unsigned index)
      : IndexedAccessorIterator<OperandIterator<ObjectType, ElementType>,
                                ObjectType, ElementType>(object, index) {}

  /// Support converting to the const variant. This will be a no-op for const
  /// variant.
  operator OperandIterator<const ObjectType, const ElementType>() const {
    return OperandIterator<const ObjectType, const ElementType>(this->object,
                                                                this->index);
  }

  ElementType *operator*() const {
    return this->object->getOperand(this->index);
  }
};

/// This template implements the result iterators for the various IR classes
/// in terms of getResult(idx).
template <typename ObjectType, typename ElementType>
class ResultIterator final
    : public IndexedAccessorIterator<ResultIterator<ObjectType, ElementType>,
                                     ObjectType, ElementType> {
public:
  /// Initializes the result iterator to the specified index.
  ResultIterator(ObjectType *object, unsigned index)
      : IndexedAccessorIterator<ResultIterator<ObjectType, ElementType>,
                                ObjectType, ElementType>(object, index) {}

  /// Support converting to the const variant. This will be a no-op for const
  /// variant.
  operator ResultIterator<const ObjectType, const ElementType>() const {
    return ResultIterator<const ObjectType, const ElementType>(this->object,
                                                               this->index);
  }

  ElementType *operator*() const {
    return this->object->getResult(this->index);
  }
};

// Implement the inline operand iterator methods.
inline auto Operation::operand_begin() -> operand_iterator {
  return operand_iterator(this, 0);
}

inline auto Operation::operand_end() -> operand_iterator {
  return operand_iterator(this, getNumOperands());
}

inline auto Operation::getOperands() -> llvm::iterator_range<operand_iterator> {
  return {operand_begin(), operand_end()};
}

inline auto Operation::operand_begin() const -> const_operand_iterator {
  return const_operand_iterator(this, 0);
}

inline auto Operation::operand_end() const -> const_operand_iterator {
  return const_operand_iterator(this, getNumOperands());
}

inline auto Operation::getOperands() const
    -> llvm::iterator_range<const_operand_iterator> {
  return {operand_begin(), operand_end()};
}

// Implement the inline result iterator methods.
inline auto Operation::result_begin() -> result_iterator {
  return result_iterator(this, 0);
}

inline auto Operation::result_end() -> result_iterator {
  return result_iterator(this, getNumResults());
}

inline auto Operation::getResults() -> llvm::iterator_range<result_iterator> {
  return {result_begin(), result_end()};
}

inline auto Operation::result_begin() const -> const_result_iterator {
  return const_result_iterator(this, 0);
}

inline auto Operation::result_end() const -> const_result_iterator {
  return const_result_iterator(this, getNumResults());
}

inline auto Operation::getResults() const
    -> llvm::iterator_range<const_result_iterator> {
  return {result_begin(), result_end()};
}
} // end namespace mlir

#endif
