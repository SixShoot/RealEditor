#include "BulkImportOperation.h"
#include "../App.h"

#include <filesystem>

#include <Tera/Cast.h>
#include <Tera/FPackage.h>
#include <Tera/UTexture.h>
#include <Tera/USoundNode.h>

#include <Utils/TextureTravaller.h>
#include <Utils/TextureProcessor.h>
#include <Utils/SoundTravaller.h>

#include <thread>

bool BulkImportOperation::Execute(ProgressWindow& progress)
{
  Errors.clear();

  // Load all packages
  int total = 0;
  std::vector<std::shared_ptr<FPackage>> packages;
  for (auto& operation : Actions)
  {
    if (!operation.IsValid())
    {
      continue;
    }

    for (auto& item : operation.Entries)
    {
      if (!item.Enabled)
      {
        continue;
      }
      std::shared_ptr<FPackage> package = nullptr;
      try
      {
        if ((package = FPackage::GetPackageNamed(item.PackageName.ToStdWstring())))
        {
          total++;
          package->Load();
          bool packageAdded = false;
          for (std::shared_ptr<FPackage> pkg : packages)
          {
            if (pkg.get() == package.get())
            {
              item.Package = package.get();
              packageAdded = true;
              break;
            }
          }

          if (packageAdded)
          {
            FPackage::UnloadPackage(package);
          }
          else
          {
            item.Package = package.get();
            packages.push_back(package);
          }
        }
        else
        {
          AddError(item.PackageName, "Failed to load\\get the package!");
        }
      }
      catch (const std::exception& e)
      {
        AddError(item.PackageName, wxString("Internal error: ") + e.what());
      }
    }
  }

  if (!packages.size())
  {
    AddError("General", "Nothing to do!");
    return false;
  }

  SendEvent(&progress, UPDATE_MAX_PROGRESS, total);
  SendEvent(&progress, UPDATE_PROGRESS_DESC, wxString::Format(wxT("Executing %d operation(s)..."), total));
  std::this_thread::sleep_for(std::chrono::seconds(1));
  
  int idx = 0;
  for (const auto& operation : Actions)
  {
    if (!operation.IsValid())
    {
      continue;
    }
    for (const auto& item : operation.Entries)
    {
      if (!item.Enabled || !item.Package)
      {
        continue;
      }

      idx++;
      SendEvent(&progress, UPDATE_PROGRESS, idx);
      SendEvent(&progress, UPDATE_PROGRESS_DESC, wxString("Processing: ") + item.Package->GetPackageName(true).WString());

      UObject* object = nullptr;
      try
      {
        if ((object = item.Package->GetObject(item.Index)))
        {
          object->Load();
        }
      }
      catch (const std::exception& e)
      {
        AddError(item.PackageName, wxString("Failed to read object: ") + e.what());
        continue;
      }

      if (!object)
      {
        AddError(item.PackageName, wxString("Failed to read object!") + item.ObjectPath.ToStdString());
        continue;
      }

      if (operation.ImportPath.size())
      {
        if (operation.ClassName == UTexture2D::StaticClassName())
        {
          ImportTexture(item.Package, Cast<UTexture2D>(object), operation.ImportPath);
        }
        else if (operation.ClassName == USoundNodeWave::StaticClassName())
        {
          ImportSound(item.Package, Cast<USoundNodeWave>(object), operation.ImportPath);
        }
        else
        {
          ImportUntyped(item.Package, object, operation.ImportPath);
        }
      }
      else if (operation.RedirectPath.size())
      {
        auto start = operation.RedirectPath.find('.');
        wxString packageName;
        if (start != wxString::npos)
        {
          packageName = operation.RedirectPath.substr(0, start);
        }
        if (packageName.empty())
        {
          AddError(item.Package->GetPackageName().WString(), "Failed to get target package!");
          continue;
        }

        try
        {
          if (std::shared_ptr<FPackage> targetPackage = FPackage::GetPackageNamed(packageName.ToStdWstring()))
          {
            targetPackage->Load();
            if (UObject* target = targetPackage->GetObject(operation.RedirectIndex))
            {
              target->Load();
              item.Package->ConvertObjectToRedirector(object, target);
            }
            else
            {
              AddError(item.Package->GetPackageName().WString(), "Failed to get redirected object!");
              FPackage::UnloadPackage(targetPackage);
              continue;
            }
            FPackage::UnloadPackage(targetPackage);
          }
        }
        catch (const std::exception& e)
        {
          AddError(item.Package->GetPackageName().WString(), e.what());
        }
      }
    }
  }

  PackageSaveContext ctx;
  ctx.EmbedObjectPath = true;
  ctx.DisableTextureCaching = true;
  SendEvent(&progress, UPDATE_PROGRESS_DESC, wxString("Saving..."));
  for (std::shared_ptr<FPackage> pkg : packages)
  {
    ctx.Path = W2A((std::filesystem::path(Path.ToStdWstring()) / pkg->GetPackageName().WString()).wstring()) + ".gpk";
    try
    {
      if (!pkg->Save(ctx))
      {
        AddError(pkg->GetPackageName(false).WString(), ctx.Error);
      }
    }
    catch (const std::exception& e)
    {
      if (ctx.Error.size())
      {
        AddError(pkg->GetPackageName(false).WString(), ctx.Error);
      }
      else
      {
        AddError(pkg->GetPackageName(false).WString(), e.what());
      }
    }
    catch (...)
    {
      AddError(pkg->GetPackageName(false).WString(), "Unknown error while saving");
    }
    FPackage::UnloadPackage(pkg);
  }
  return true;
}

void BulkImportOperation::AddError(const wxString& source, const wxString& error)
{
  Errors.emplace_back(std::make_pair(source, error ));
}

void BulkImportOperation::ImportTexture(FPackage* package, UTexture2D* texture, const wxString& source)
{
  if (!texture)
  {
    AddError(package->GetPackageName().WString(), "Object is not a texture!");
    return;
  }

  TextureProcessor::TCFormat inputFormat = TextureProcessor::TCFormat::None;
  wxString extension;
  wxFileName::SplitPath(source, nullptr, nullptr, nullptr, &extension);
  extension.MakeLower();
  if (extension == "tga")
  {
    inputFormat = TextureProcessor::TCFormat::TGA;
  }
  else if (extension == "png")
  {
    inputFormat = TextureProcessor::TCFormat::PNG;
  }
  else if (extension == "dds")
  {
    inputFormat = TextureProcessor::TCFormat::DDS;
  }
  else
  {
    AddError(package->GetPackageName().WString(), wxString("Can't import ") + extension + " files");
    return;
  }

  TextureProcessor processor(inputFormat, TextureProcessor::TCFormat::None);
  processor.SetInputPath(W2A(source.ToStdWstring()));
  EPixelFormat processorFormat = PF_Unknown;

  bool isNormal = texture->CompressionSettings == TC_Normalmap ||
    texture->CompressionSettings == TC_NormalmapAlpha ||
    texture->CompressionSettings == TC_NormalmapUncompressed ||
    texture->CompressionSettings == TC_NormalmapBC5;

  switch ((processorFormat = texture->Format))
  {
  case PF_DXT1:
    processor.SetOutputFormat(TextureProcessor::TCFormat::DXT1);
    break;
  case PF_DXT3:
    processor.SetOutputFormat(TextureProcessor::TCFormat::DXT3);
    break;
  case PF_DXT5:
    processor.SetOutputFormat(TextureProcessor::TCFormat::DXT5);
    break;
  case PF_A8R8G8B8:
    processor.SetOutputFormat(TextureProcessor::TCFormat::ARGB8);
    break;
  case PF_G8:
    processor.SetOutputFormat(TextureProcessor::TCFormat::G8);
    break;
  default:
    AddError(package->GetPackageName().WString(), wxString("Can't import to textures with 0x") + std::to_string(texture->Format) + " pixel format.");
    return;
  }

  processor.SetSrgb(texture->SRGB);
  processor.SetNormal(isNormal);
  processor.SetGenerateMips(false); //  TODO: change to true when mips don't crash the game
  processor.SetAddressX(texture->AddressX);
  processor.SetAddressY(texture->AddressY);
  processor.ClearOutput();

  if (!processor.Process())
  {
    AddError(package->GetPackageName().WString(), processor.GetError());
    return;
  }

  TextureTravaller travaller;
  travaller.SetFormat(processorFormat);
  travaller.SetAddressX(texture->AddressX);
  travaller.SetAddressY(texture->AddressY);

  const auto& mips = processor.GetOutputMips();
  for (const auto mip : mips)
  {
    travaller.AddMipMap(mip.SizeX, mip.SizeY, mip.Size, mip.Data);
  }

  if (!travaller.Visit(texture))
  {
    AddError(package->GetPackageName().WString(), travaller.GetError());
  }
}

void BulkImportOperation::ImportSound(FPackage* package, USoundNodeWave* sound, const wxString& source)
{
  if (!sound)
  {
    AddError(package->GetPackageName().WString(), "Object is not a sound node!");
    return;
  }

  SoundTravaller travaller;
  try
  {
    void* soundData = nullptr;
    FILE_OFFSET size = 0;

    std::ifstream s(source.ToStdWstring(), std::ios::in | std::ios::binary);
    size_t tmpPos = s.tellg();
    s.seekg(0, std::ios::end);
    size = (FILE_OFFSET)s.tellg();
    s.seekg(tmpPos);
    if (size > 0)
    {
      soundData = malloc(size);
      s.read((char*)soundData, size);
      travaller.SetData(soundData, size);
    }
    else
    {
      AddError(package->GetPackageName().WString(), wxString("File is empty: ") + source);
      return;
    }
  }
  catch (const std::exception& e)
  {
    AddError(package->GetPackageName().WString(), e.what());
  }
  catch (...)
  {
    AddError(package->GetPackageName().WString(), "Unknown error!");
  }

  if (!travaller.Visit(sound))
  {
    AddError(package->GetPackageName().WString(), "Failed to import data!");
  }
}

void BulkImportOperation::ImportUntyped(FPackage* package, UObject* tobject, const wxString& source)
{
  if (!tobject)
  {
    AddError(package->GetPackageName().WString(), "Internal error! No object found!");
    return;
  }

  void* rawData = nullptr;
  FILE_OFFSET size = 0;

  try
  {
    std::ifstream s(source.ToStdWstring(), std::ios::in | std::ios::binary);
    size_t tmpPos = s.tellg();
    s.seekg(0, std::ios::end);
    size = (FILE_OFFSET)s.tellg();
    s.seekg(tmpPos);
    if (size > 0)
    {
      rawData = malloc(size);
      s.read((char*)rawData, size);
    }
    else
    {
      AddError(package->GetPackageName().WString(), wxString("File is empty: ") + source);
      return;
    }
  }
  catch (const std::exception& e)
  {
    AddError(package->GetPackageName().WString(), e.what());
    return;
  }

  tobject->SetRawData(rawData, size);
  free(rawData);
}
