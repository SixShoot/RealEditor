#include "UObjectRedirector.h"
#include "FPackage.h"
#include "FName.h"

void UObjectRedirector::Serialize(FStream& s)
{
  Super::Serialize(s);
  SERIALIZE_UREF(s, Object);
}