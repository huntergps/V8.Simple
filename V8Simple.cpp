#include "V8Simple.h"
#include <include/v8-debug.h>
#include <include/libplatform/libplatform.h>
#include <stdlib.h>
#include <string>

namespace V8Simple
{

String::String(const char* value)
	: String(value, value == nullptr ? 0 : static_cast<int>(std::strlen(value)))
{
}

String::String(const char* value, int length)
	: _value(new char[length + 1])
	, _length(length)
{
	if (length > 0 && value != nullptr)
	{
		std::memcpy(_value, value, _length);
	}
	_value[_length] = 0;
}

String& String::operator=(const String& str)
{
	delete[] _value;
	_length = str._length;
	_value = new char[_length + 1];
	std::memcpy(_value, str._value, _length + 1);
	return *this;
}

String::String(const String& str)
	: String(str._value, str._length)
{
}

String::String(const v8::String::Utf8Value& v)
	: String(v.length() > 0 ? *v : nullptr, v.length())
{
}

Type String::GetValueType() const { return Type::String; }
const char* String::GetValue() const { return _value; }

String::~String()
{
	delete[] _value;
}

v8::Isolate* CurrentIsolate()
{
	return Context::_globalContext->_isolate;
}

v8::Local<v8::Context> CurrentContext()
{
	return Context::_globalContext->_context->Get(CurrentIsolate());
}

struct V8Scope
{
	V8Scope(v8::Isolate* isolate, v8::Persistent<v8::Context>* context)
		: Locker(isolate)
		, IsolateScope(isolate)
		, HandleScope(isolate)
		, ContextScope(context->Get(isolate))
		, TryCatch(isolate)
	{
	}
	V8Scope()
		: V8Scope(CurrentIsolate(), Context::_globalContext->_context)
	{
	}
	v8::Locker Locker;
	v8::Isolate::Scope IsolateScope;
	v8::HandleScope HandleScope;
	v8::Context::Scope ContextScope;
	v8::TryCatch TryCatch;
};

void Throw(const V8Scope& scope)
{
	auto context = CurrentContext();
	v8::Local<v8::Value> emptyString = v8::String::Empty(CurrentIsolate());

	auto message = scope.TryCatch.Message();
	auto sourceLine(emptyString);
	auto messageStr(emptyString);
	auto fileName(emptyString);
	int lineNumber = -1;
	if (!message.IsEmpty())
	{
		sourceLine = message->GetSourceLine(context).FromMaybe(emptyString);
		auto messageStrLocal = message->Get();
		if (!messageStrLocal.IsEmpty())
		{
			messageStr = messageStrLocal;
		}
		fileName = message->GetScriptResourceName();
		lineNumber = message->GetLineNumber(context).FromMaybe(-1);
	}

	v8::Local<v8::Value> exception = scope.TryCatch.Exception().IsEmpty()
		? emptyString
		: scope.TryCatch.Exception();

	auto stackTrace(
		scope.TryCatch
		.StackTrace(context)
		.FromMaybe(emptyString.As<v8::Value>()));

	throw ScriptException(
		v8::String::Utf8Value(exception),
		v8::String::Utf8Value(messageStr),
		v8::String::Utf8Value(fileName),
		lineNumber,
		v8::String::Utf8Value(stackTrace),
		v8::String::Utf8Value(sourceLine));
}

template<class A>
static v8::Local<A> FromJust(
	const V8Scope& scope,
	v8::MaybeLocal<A> a)
{
	if (scope.TryCatch.HasCaught() || a.IsEmpty())
	{
		Throw(scope);
	}
	return a.ToLocalChecked();
}

template<class A>
static A FromJust(
	const V8Scope& scope,
	v8::Maybe<A> a)
{
	if (scope.TryCatch.HasCaught() || a.IsNothing())
	{
		Throw(scope);
	}
	return a.FromJust();
}

static v8::Local<v8::String> ToV8String(
	const V8Scope& scope,
	const char* str)
{
	return FromJust(scope, v8::String::NewFromUtf8(CurrentIsolate(), str, v8::NewStringType::kNormal));
}

Value* Value::Wrap(
	const V8Scope& scope,
	v8::Local<v8::Value> value)
{
	auto context = CurrentContext();
	if (value->IsInt32())
	{
		return new Int(FromJust(
			scope,
			value->Int32Value(context)));
	}
	if (value->IsNumber() || value->IsNumberObject())
	{
		return new Double(FromJust(
			scope,
			value->NumberValue(context)));
	}
	if (value->IsBoolean() || value->IsBooleanObject())
	{
		return new Bool(FromJust(
			scope,
			value->BooleanValue(context)));
	}
	if (value->IsString() || value->IsStringObject())
	{
		v8::String::Utf8Value str(FromJust(
			scope,
			value->ToString(context)));
		return new String(*str, str.length());
	}
	if (value->IsArray())
	{
		return new Array(FromJust(
			scope,
			value->ToObject(context)).As<v8::Array>());
	}
	if (value->IsFunction())
	{
		return new Function(FromJust(
			scope,
			value->ToObject(context)).As<v8::Function>());
	}
	if (value->IsObject())
	{
		return new Object(FromJust(
			scope,
			value->ToObject(context)));
	}
	if (value->IsUndefined() || value->IsNull())
	{
		return nullptr;
	}
	throw std::runtime_error("Unhandled type in V8Simple");
}

Value* Value::Wrap(
	const V8Scope& scope,
	v8::MaybeLocal<v8::Value> mvalue)
{
	return Wrap(scope, FromJust(scope, mvalue));
}

v8::Local<v8::Value> Value::Unwrap(
	const V8Scope& scope,
	Value* value)
{
	auto isolate = CurrentIsolate();

	if (value == nullptr)
	{
		return v8::Null(isolate).As<v8::Value>();
	}

	switch (value->GetValueType())
	{
		case Type::Int:
			return v8::Int32::New(
				isolate,
				static_cast<Int*>(value)->GetValue());
		case Type::Double:
			return v8::Number::New(
				isolate,
				static_cast<Double*>(value)->GetValue());
		case Type::String:
			return ToV8String(
				scope,
				static_cast<String*>(value)->GetValue());
		case Type::Bool:
			return v8::Boolean::New(
				isolate,
				static_cast<Bool*>(value)->GetValue());
		case Type::Object:
			return static_cast<Object*>(value)
				->_object.Get(isolate);
		case Type::Array:
			return static_cast<Array*>(value)
				->_array.Get(isolate);
		case Type::Function:
			return static_cast<Function*>(value)
				->_function.Get(isolate);
		case Type::Callback:
			Callback* callback = static_cast<Callback*>(value);
			callback->Retain();
			auto localCallback = v8::External::New(isolate, callback);
			v8::Persistent<v8::External> persistentCallback(isolate, localCallback);

			persistentCallback.SetWeak(
				callback,
				[] (const v8::WeakCallbackInfo<Callback>& data)
				{
					auto cb = data.GetParameter();
					cb->Release();
				},
				v8::WeakCallbackType::kParameter);

			return FromJust(scope, v8::Function::New(
				CurrentContext(),
				[] (const v8::FunctionCallbackInfo<v8::Value>& info)
				{
					V8Scope scope;

					std::vector<Value*> wrappedArgs;
					wrappedArgs.reserve(info.Length());
					try
					{
						for (int i = 0; i < info.Length(); ++i)
						{
							wrappedArgs.push_back(Wrap(
								scope,
								info[i]));
						}
					}
					catch (const std::runtime_error& e)
					{
						Context::HandleRuntimeException(e.what());
					}

					Callback* callback =
						static_cast<Callback*>(info.Data()
							.As<v8::External>()
							->Value());
					UniqueValueVector args(wrappedArgs);
					Value* result = callback->Call(args);
					int len = args.Length();
					for (int i = 0; i < len; ++i)
					{
						delete args.Get(i);
					}

					info.GetReturnValue().Set(Unwrap(
						scope,
						result));
				},
				localCallback.As<v8::Value>()));
		}
	return v8::Null(isolate).As<v8::Value>();
}

std::vector<v8::Local<v8::Value>> Value::UnwrapVector(
	const V8Scope& scope,
	const std::vector<Value*>& values)
{
	std::vector<v8::Local<v8::Value>> result;
	result.reserve(values.size() + 1);
	for (Value* value: values)
	{
		result.push_back(Unwrap(scope, value));
	}
	return result;
}

Object::Object(v8::Local<v8::Object> object)
	: _object(CurrentIsolate(), object)
{ }

Type Object::GetValueType() const { return Type::Object; }

Value* Object::Get(const char* key)
{
	if (key == nullptr)
	{
		Context::HandleRuntimeException("V8Simple::Object::Get is not defined for nullptr argument");
		return nullptr;
	}
	try
	{
		V8Scope scope;

		return Wrap(
			scope,
			_object.Get(CurrentIsolate())->Get(
				CurrentContext(),
				ToV8String(scope, key)));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
		return nullptr;
	}
	catch (const std::runtime_error& e)
	{
		Context::HandleRuntimeException(e.what());
		return nullptr;
	}
}

void Object::Set(const char* key, Value* value)
{
	if (key == nullptr)
	{
		Context::HandleRuntimeException("V8Simple::Object::Set is not defined for nullptr `key` argument");
		return;
	}
	try
	{
		V8Scope scope;

		auto ret = _object.Get(CurrentIsolate())->Set(
			CurrentContext(),
			ToV8String(scope, key),
			Unwrap(scope, value));
		FromJust(scope, ret);
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
	}
}

std::vector<String> Object::Keys()
{
	try
	{
		V8Scope scope;
		auto context = CurrentContext();

		auto propArr = FromJust(
			scope,
			_object.Get(CurrentIsolate())->GetPropertyNames(context));

		auto length = propArr->Length();
		std::vector<String> result;
		result.reserve(length);
		for (int i = 0; i < static_cast<int>(length); ++i)
		{
			result.push_back(String(v8::String::Utf8Value(FromJust(
				scope,
				propArr->Get(context, static_cast<uint32_t>(i))))));
		}
		return result;
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
		return std::vector<String>();
	}
}

bool Object::InstanceOf(Function& type)
{
	Value* callResult = nullptr;
	try
	{
		std::vector<Value*> args;
		args.reserve(2);
		args.push_back(this);
		args.push_back(&type);
		callResult = Context::_globalContext->_instanceOf->Call(args);
		bool result = (callResult == nullptr || callResult->GetValueType() != Type::Bool)
			? false
			: static_cast<Bool*>(callResult)->GetValue();
		delete callResult;
		return result;
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
	}
	catch (const std::runtime_error& e)
	{
		Context::HandleRuntimeException(e.what());
	}
	delete callResult;
	return false;
}

// Workaround for pre-C++11
template<class T>
static T* DataPointer(std::vector<T>& v)
{
	if (v.size() == 0)
	{
		return nullptr;
	}
	return &v[0];
}

Value* Object::CallMethod(
	const char* name,
	const std::vector<Value*>& args)
{
	if (name == nullptr)
	{
		Context::HandleRuntimeException("V8Simple::Object::CallMethod is not defined for nullptr `name` argument");
		return nullptr;
	}
	try
	{
		V8Scope scope;
		auto isolate = CurrentIsolate();
		auto context = CurrentContext();

		auto localObject = _object.Get(isolate);
		auto prop = FromJust(
			scope,
			localObject->Get(
				context,
				ToV8String(scope, name).As<v8::Value>()));

		if (!prop->IsFunction())
		{
			Throw(scope);
		}
		auto fun = prop.As<v8::Function>();

		auto unwrappedArgs = UnwrapVector(scope, args);
		return Wrap(
			scope,
			fun->Call(
				localObject,
				static_cast<int>(unwrappedArgs.size()),
				DataPointer(unwrappedArgs)));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
	}
	catch (const std::runtime_error& e)
	{
		Context::HandleRuntimeException(e.what());
	}
	return nullptr;
}

bool Object::ContainsKey(const char* key)
{
	if (key == nullptr)
	{
		Context::HandleRuntimeException("V8Simple::Object::ContainsKey is not defined for nullptr");
		return false;
	}
	try
	{
		V8Scope scope;

		return FromJust(
			scope,
			_object.Get(CurrentIsolate())->Has(
				CurrentContext(),
				ToV8String(scope, key)));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
		return false;
	}
}

bool Object::Equals(const Object& o)
{
	try
	{
		V8Scope scope;
		auto isolate = CurrentIsolate();

		return FromJust(
			scope,
			_object.Get(isolate)->Equals(
				CurrentContext(),
				o._object.Get(isolate)));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
		return false;
	}
}

Function::Function(v8::Local<v8::Function> function)
	: _function(CurrentIsolate(), function)
{
}

Type Function::GetValueType() const { return Type::Function; }

Value* Function::Call(const std::vector<Value*>& args)
{
	try
	{
		V8Scope scope;
		auto context = CurrentContext();

		auto unwrappedArgs = UnwrapVector(scope, args);
		return Wrap(
			scope,
			_function.Get(CurrentIsolate())->Call(
				context,
				context->Global(),
				static_cast<int>(unwrappedArgs.size()),
				DataPointer(unwrappedArgs)));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
	}
	catch (const std::runtime_error& e)
	{
		Context::HandleRuntimeException(e.what());
	}
	return nullptr;
}

Object* Function::Construct(const std::vector<Value*>& args)
{
	try
	{
		V8Scope scope;
		auto isolate = CurrentIsolate();

		auto unwrappedArgs = UnwrapVector(scope, args);

		return new Object(
			FromJust(
				scope,
				_function.Get(isolate)->NewInstance(
					CurrentContext(),
					static_cast<int>(unwrappedArgs.size()),
					DataPointer(unwrappedArgs))));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
	}
	return nullptr;
}

bool Function::Equals(const Function& function)
{
	try
	{
		V8Scope scope;
		auto isolate = CurrentIsolate();

		return FromJust(
			scope,
			_function.Get(isolate)->Equals(
				CurrentContext(),
				function._function.Get(isolate)));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
	}
	return false;
}

Array::Array(v8::Local<v8::Array> array)
	: _array(CurrentIsolate(), array)
{ }

Type Array::GetValueType() const { return Type::Array; }

Value* Array::Get(int index)
{
	try
	{
		V8Scope scope;

		return Wrap(
			scope,
			_array.Get(CurrentIsolate())->Get(
				CurrentContext(),
				static_cast<uint32_t>(index)));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
		return nullptr;
	}
	catch (const std::runtime_error& e)
	{
		Context::HandleRuntimeException(e.what());
		return nullptr;
	}
}

void Array::Set(int index, Value* value)
{
	try
	{
		V8Scope scope;
		auto isolate = CurrentIsolate();

		FromJust(
			scope,
			_array.Get(isolate)->Set(
				CurrentContext(),
				static_cast<uint32_t>(index),
				Unwrap(scope, value)));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
	}
}

int Array::Length()
{
	V8Scope scope;

	return static_cast<int>(_array.Get(CurrentIsolate())->Length());
}

bool Array::Equals(const Array& array)
{
	try
	{
		V8Scope scope;
		auto isolate = CurrentIsolate();

		return FromJust(
			scope,
			_array.Get(isolate)->Equals(
				CurrentContext(),
				array._array.Get(isolate)));
	}
	catch (const ScriptException& e)
	{
		Context::HandleScriptException(e);
		return false;
	}

}

UniqueValueVector::UniqueValueVector(std::vector<Value*>& values)
	: _values(values)
{
}

int UniqueValueVector::Length()
{
	return static_cast<int>(_values.size());
}

Value* UniqueValueVector::Get(int index)
{
	Value* result = nullptr;
	std::swap(_values.at(index), result);
	return result;
}

Callback::Callback()
{
}

Value* Callback::Call(UniqueValueVector args)
{
	return nullptr;
}

Type Callback::GetValueType() const { return Type::Callback; }

struct ArrayBufferAllocator: ::v8::ArrayBuffer::Allocator
{
	virtual void* Allocate(size_t length)
	{
		return calloc(length, 1);
	}

	virtual void* AllocateUninitialized(size_t length)
	{
		return malloc(length);
	}

	virtual void Free(void* data, size_t)
	{
		free(data);
	}
};

ScriptException::ScriptException(
	const ::v8::String::Utf8Value& name,
	const ::v8::String::Utf8Value& errorMessage,
	const ::v8::String::Utf8Value& fileName,
	int lineNumber,
	const ::v8::String::Utf8Value& stackTrace,
	const ::v8::String::Utf8Value& sourceLine)
	: Name(name)
	, ErrorMessage(errorMessage)
	, FileName(fileName)
	, StackTrace(stackTrace)
	, SourceLine(sourceLine)
	, LineNumber(lineNumber)
{
}

void Context::HandleScriptException(const ScriptException& e)
{
	if (_globalContext->_scriptExceptionHandler != nullptr)
	{
		_globalContext->_scriptExceptionHandler->Handle(e);
	}
}

void Context::HandleRuntimeException(const char* e)
{
	if (_globalContext != nullptr && _globalContext->_runtimeExceptionHandler != nullptr)
	{
		_globalContext->_runtimeExceptionHandler->Handle(e);
	}
}

void Context::SetDebugMessageHandler(MessageHandler* debugMessageHandler)
{
	if (_globalContext != nullptr)
	{
		if (debugMessageHandler != nullptr)
		{
			debugMessageHandler->Retain();
		}

		if (_globalContext->_debugMessageHandler != nullptr)
		{
			_globalContext->_debugMessageHandler->Release();
		}

		V8Scope scope;

		if (debugMessageHandler == nullptr)
		{
			v8::Debug::SetMessageHandler(nullptr);
		}
		else
		{
			v8::Debug::SetMessageHandler([] (const v8::Debug::Message& message)
			{
				v8::String::Utf8Value str(message.GetJSON());
				_globalContext->_debugMessageHandler->Handle(String(*str, str.length()));
			});
		}
		_globalContext->_debugMessageHandler = debugMessageHandler;
	}
}

void Context::SendDebugCommand(const char* command)
{
	if (command == nullptr)
	{
		Context::HandleRuntimeException("V8Simple::Context::SendDebugCommand is not defined for nullptr argument");
		return;
	}
	if (_globalContext != nullptr)
	{
		v8::Locker locker(_globalContext->_conversionIsolate);
		v8::Isolate::Scope isolateScope(_globalContext->_conversionIsolate);
		v8::HandleScope handleScope(_globalContext->_conversionIsolate);

		auto str = v8::String::NewFromUtf8(_globalContext->_conversionIsolate, command, v8::NewStringType::kNormal).FromMaybe(v8::String::Empty(_globalContext->_conversionIsolate));
		auto len = str->Length();
		auto buffer = new uint16_t[len + 1];
		str->Write(buffer);
		v8::Debug::SendCommand(_globalContext->_isolate, buffer, len);
		delete[] buffer;
	}
}

void Context::ProcessDebugMessages()
{
	if (_globalContext != nullptr)
	{
		V8Scope scope;
		v8::Debug::ProcessDebugMessages();
	}
}

Context* Context::_globalContext = nullptr;
v8::Platform* Context::_platform = nullptr;

Context::Context(ScriptExceptionHandler* scriptExceptionHandler, MessageHandler* runtimeExceptionHandler)
{
	if (_globalContext != nullptr)
	{
		HandleRuntimeException("V8Simple::Contexts are not re-entrant");
		return;
	}

	_globalContext = this;
	_debugMessageHandler = nullptr;

	_scriptExceptionHandler = scriptExceptionHandler;
	if (_scriptExceptionHandler != nullptr)
	{
		_scriptExceptionHandler->Retain();
	}

	_runtimeExceptionHandler = runtimeExceptionHandler;
	if (_runtimeExceptionHandler != nullptr)
	{
		_runtimeExceptionHandler->Retain();
	}

	// v8::V8::SetFlagsFromString("--expose-gc", 11);
	{
		// For compatibility with node.js
		const char flags[] = "--expose_debug_as=v8debug";
		v8::V8::SetFlagsFromString(flags, sizeof(flags));
	}

	if (_platform == nullptr)
	{
		v8::V8::InitializeICU();
		_platform = v8::platform::CreateDefaultPlatform();
		v8::V8::InitializePlatform(_platform);
		v8::V8::Initialize();
	}

	{
		v8::Isolate::CreateParams createParams;
		static ArrayBufferAllocator arrayBufferAllocator;
		createParams.array_buffer_allocator = &arrayBufferAllocator;
		_isolate = v8::Isolate::New(createParams);
		_conversionIsolate = v8::Isolate::New(createParams);
	}

	{
		v8::Locker locker(_isolate);
		v8::Isolate::Scope isolateScope(_isolate);
		v8::HandleScope handleScope(_isolate);

		auto localContext = v8::Context::New(_isolate);
		v8::Context::Scope contextScope(localContext);

		_context = new v8::Persistent<v8::Context>(_isolate, localContext);
	}
	
	Value* instanceOf = Evaluate(
		"instanceof",
		"(function(x, y) { return (x instanceof y); })");
	if (!instanceOf || instanceOf->GetValueType() != Type::Function)
	{
		Context::HandleRuntimeException("V8Simple could not create an instanceof function");
	}
	else
	{
		_instanceOf = static_cast<Function*>(instanceOf);
	}
}

Context::~Context()
{
	{
		v8::Locker locker(_isolate);
		v8::Isolate::Scope isolateScope(_isolate);
		v8::HandleScope handleScope(_isolate);

		{
			v8::Context::Scope contextScope(_context->Get(_isolate));
			delete _instanceOf;

			if (_runtimeExceptionHandler != nullptr)
			{
				_runtimeExceptionHandler->Release();
			}
			_runtimeExceptionHandler = nullptr;

			if (_scriptExceptionHandler != nullptr)
			{
				_scriptExceptionHandler->Release();
			}
			_scriptExceptionHandler = nullptr;
		}

		delete _context;
	}

	_conversionIsolate->Dispose();
	_isolate->Dispose();

	_globalContext = nullptr;

	// If we do this we can't create a new context afterwards.
	//
	// v8::V8::Dispose();
	// v8::V8::ShutdownPlatform();
	// delete _platform;
}

Value* Context::Evaluate(const char* fileName, const char* code)
{
	if (fileName == nullptr)
	{
		Context::HandleRuntimeException("V8Simple::Context::Evaluate is not defined for nullptr `fileName` argument");
		return nullptr;
	}
	if (code == nullptr)
	{
		Context::HandleRuntimeException("V8Simple::Context::Evaluate is not defined for nullptr `code` argument");
		return nullptr;
	}
	try
	{
		V8Scope scope;
		auto context = CurrentContext();

		v8::ScriptOrigin origin(ToV8String(scope, fileName));
		auto script = FromJust(
			scope,
			v8::Script::Compile(
				context,
				ToV8String(scope, code),
				&origin));

		return Value::Wrap(scope, script->Run(context));
	}
	catch (const ScriptException& e)
	{
		HandleScriptException(e);
		return nullptr;
	}
	catch (const std::runtime_error& e)
	{
		HandleRuntimeException(e.what());
		return nullptr;
	}
}

Object* Context::GlobalObject()
{
	V8Scope scope;

	return new Object(CurrentContext()->Global());
}

bool Context::IdleNotificationDeadline(double deadline_in_seconds)
{
	// _isolate->RequestGarbageCollectionForTesting(v8::Isolate::kFullGarbageCollection);
	return _isolate->IdleNotificationDeadline(deadline_in_seconds);
}

} // namespace V8Simple
