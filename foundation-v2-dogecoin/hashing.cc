#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <stdint.h>
#include <iostream>
#include "nan.h"

// Main Imports
#include "algorithms/scrypt/scrypt.h"
#include "algorithms/scrypt/utils/sha256.h"
#include "algorithms/sha256d/sha256d.h"

using namespace node;
using namespace v8;

#define THROW_ERROR_EXCEPTION(x) Nan::ThrowError(x)
const char* ToCString(const Nan::Utf8String& value) {
  return *value ? *value : "<string conversion failed>";
}

// Scrypt Algorithm
NAN_METHOD(scrypt) {

  // Handle Main Scope
  Isolate* isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);

  // Check Arguments for Errors [1]
  if (info.Length() < 3)
    return THROW_ERROR_EXCEPTION("You must provide an input buffer, as well as an nValue and rValue.");
  if (!info[1]->IsInt32() || !info[2]->IsInt32())
    return THROW_ERROR_EXCEPTION("The first and second parameters should be scrypt parameters (n, r)");

  // Define Passed Parameters
  Isolate *argsIsolate = info.GetIsolate();
  Local<Context> context = argsIsolate->GetCurrentContext();
  Local<Object> header = info[0]->ToObject(context).ToLocalChecked();
  unsigned int N = info[1].As<Uint32>()->Value();
  unsigned int R = info[2].As<Uint32>()->Value();

  // Check Arguments for Errors [2]
  if (!Buffer::HasInstance(header))
    return THROW_ERROR_EXCEPTION("Argument should be a buffer object.");

  // Process/Define Passed Parameters
  char * input = Buffer::Data(header);
  uint32_t input_len = Buffer::Length(header);
  char output[32];

  // Hash Input Data and Return Output
  scrypt_N_R_1_256(input, output, N, R, input_len);
  info.GetReturnValue().Set(Nan::CopyBuffer(output, 32).ToLocalChecked());
}

// Sha256d Algorithm
NAN_METHOD(sha256d) {

  // Check Arguments for Errors
  if (info.Length() < 1)
    return THROW_ERROR_EXCEPTION("You must provide one argument.");

  // Process/Define Passed Parameters
  char * input = Buffer::Data(Nan::To<v8::Object>(info[0]).ToLocalChecked());
  uint32_t input_len = Buffer::Length(Nan::To<v8::Object>(info[0]).ToLocalChecked());
  char output[32];

  // Hash Input Data and Return Output
  sha256d_hash(input, output, input_len);
  info.GetReturnValue().Set(Nan::CopyBuffer(output, 32).ToLocalChecked());
}

NAN_MODULE_INIT(init) {
  Nan::Set(target, Nan::New("scrypt").ToLocalChecked(), Nan::GetFunction(Nan::New<FunctionTemplate>(scrypt)).ToLocalChecked());
  Nan::Set(target, Nan::New("sha256d").ToLocalChecked(), Nan::GetFunction(Nan::New<FunctionTemplate>(sha256d)).ToLocalChecked());
}

NODE_MODULE(hashing, init)
