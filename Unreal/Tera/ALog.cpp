#include "../../App/LogWindow.h"

#include "ALog.h"
#include "FStream.h"


ALog* SharedLogger = nullptr;

wxPoint FVectorToPoint(const FVector2D& v)
{
  const int x = v.X;
  const int y = v.Y;
  if (x == -1 && y == -1)
  {
    return wxDefaultPosition;
  }
  return wxPoint(x, y);
}

ALog* ALog::SharedLog()
{
  if (!SharedLogger)
  {
    SharedLogger = new ALog();
  }
  return SharedLogger;
}

void ALog::Log(const std::string& msg, ALogEntry::Type channel)
{
  SharedLogger->Push({ std::time(nullptr), msg, channel });
}

void ALog::ILog(const std::string& msg)
{
  Log(msg, ALogEntry::Type::INFO);
}

void ALog::ELog(const std::string& msg)
{
  Log(msg, ALogEntry::Type::ERR);
}

void ALog::WLog(const std::string& msg)
{
  Log(msg, ALogEntry::Type::WARN);
}

void ALog::GetConfig(FLogConfig& cfg)
{
  ALog::SharedLog()->_GetConfig(cfg);
}

void ALog::SetConfig(const FLogConfig& cfg)
{
  ALog::SharedLog()->_SetConfig(cfg);
}

void ALog::_GetConfig(FLogConfig& cfg)
{
  ALog* l = ALog::SharedLog();
  cfg.LogPosition = l->LastPosition;
  cfg.LogSize = l->LastSize;
}

void ALog::_SetConfig(const FLogConfig& cfg)
{
  ALog* l = ALog::SharedLog();
  std::lock_guard<std::recursive_mutex> locker(WLocker);
  LastPosition = cfg.LogPosition;
  LastSize = cfg.LogSize;
  if (Window)
  {
    Window->SetPosition(FVectorToPoint(cfg.LogPosition));
    Window->SetSize(wxSize(cfg.LogSize.X, cfg.LogSize.Y));
  }
}

ALog::ALog()
{
}

void ALog::Show(bool show)
{
  ALog::SharedLog()->_Show(show);
}

bool ALog::IsShown()
{
  return ALog::SharedLog()->_IsShown();
}

bool ALog::_IsShown()
{
  std::lock_guard<std::recursive_mutex> locker(WLocker);
  return Window ? Window->IsShown() : false;
}

void ALog::_Show(bool show)
{
  std::lock_guard<std::recursive_mutex> lock(WLocker);
  if (Window)
  {
    if (!show)
    {
      Window->Close();
    }
  }
  else if (show)
  {
    Window = new LogWindow(nullptr, wxID_ANY, FVectorToPoint(LastPosition), wxSize(LastSize.X, LastSize.Y));
    Window->SetLogger(this);
    Window->SetPosition(FVectorToPoint(LastPosition));
    Window->SetSize(wxSize(LastSize.X, LastSize.Y));
    Window->Show(show);
    UpdateWindow();
  }
}

void ALog::OnLogClose()
{
  std::lock_guard<std::recursive_mutex> lock(WLocker);
  LastPosition.X = Window->GetPosition().x;
  LastPosition.Y = Window->GetPosition().y;
  LastSize.X = Window->GetSize().x;
  LastSize.Y = Window->GetSize().y;
  Window = nullptr;
}

void ALog::OnAppExit()
{
  if (SharedLogger)
  {
    delete SharedLogger;
  }
}

void ALog::GetEntries(std::vector<ALogEntry>& output, size_t& index)
{
  std::lock_guard<std::recursive_mutex> lock(ELocker);
  while (index < Entries.size())
  {
    output.push_back(Entries[index]);
    index++;
  }
}

void ALog::Push(const ALogEntry& entry)
{
  {
    std::lock_guard<std::recursive_mutex> lock(ELocker);
    Entries.push_back(entry);
  }
  std::lock_guard<std::recursive_mutex> lock(WLocker);
  if (Window)
  {
    UpdateWindow();
  }
}

void ALog::UpdateWindow()
{
  std::lock_guard<std::recursive_mutex> lock(WLocker);
  if (Window)
  {
    wxCommandEvent* event = new wxCommandEvent(PUMP_LOG_WINDOW);
    wxQueueEvent(Window, event);
  }
}
