// Included after the unchanged ToonGPUProbe.cpp in an isolated host project only.
#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "MaterialShared.h"
#include "RenderGraphBuilder.h"
#include "RHICommandList.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace ToonLive
{
using namespace ToonGPU;
void SaveJSON(const TSharedRef<FJsonObject>& O,const FString& Path)
{FString S;auto W=TJsonWriterFactory<>::Create(&S);FJsonSerializer::Serialize(O,W);FFileHelper::SaveStringToFile(S,*Path);}
TSharedPtr<FJsonObject> Stats()
{FShaderCompilerStats S;GShaderCompilingManager->GetLocalStats(S);return S.ToJson();}
void SetRamp(UToonProfile* P,float Tau,float Width,bool Smooth=false)
{
    TArray<TPair<float,FLinearColor>> Keys;
    // Restrict the reference function, rather than clamping knot positions. Handles endpoints and overlaps consistently.
    for(int32 I=0;I<=256;++I)
    {
        const float U=I/256.f,Q=2*U-1;
        float Z=Width>0?FMath::Clamp((Q-Tau+Width)/(2*Width),0.f,1.f):(Q<Tau?0.f:Q>Tau?1.f:.5f);
        if(Smooth)Z=Z*Z*(3-2*Z);
        Keys.Add({U,FLinearColor(.1f+.9f*Z,.1f+.9f*Z,.1f+.9f*Z,1)});
    }
    Curve(P->Settings.DiffuseRamp,Keys);
}
void Notify(UToonProfile* P)
{
    auto* Property=FindFProperty<FProperty>(UToonProfile::StaticClass(),TEXT("Settings"));
    FPropertyChangedEvent Event(Property,EPropertyChangeType::Interactive);P->PostEditChangeProperty(Event);
}
struct FStep
{
    FString Name,Route=TEXT("A"),Material=TEXT("LiveA");float Tau=.5f,Width=.08f;
    bool Shared=false,Slots=false,Self=false,Inside=false,Smooth=false,Natural=false,NotifyTexture=true;
    int32 Submissions=1;
};
struct FState
{
    TSharedPtr<FFixture> Base=MakeShared<FFixture>();TArray<FStep> Steps;TArray<TSharedPtr<FJsonValue>> Rows;
    UTexture2D* Offset=nullptr;UTexture2D* DynamicOffset=nullptr;UStaticMesh* SlotsMesh=nullptr;UStaticMesh* Sphere=nullptr;
    UMaterialInstanceDynamic* Right=nullptr;FString Out,AssetDir;int32 Index=0;
    TSharedPtr<FJsonObject> InitialStats;bool Reload=false;
};
struct FAtlas
{
    bool Invalid=false,Valid=false;int32 X=0,Y=0,Slices=0;double CPU=0,GPU=-1,DispatchGPU=-1;
};
FAtlas ProbeAtlas()
{
    FAtlas A;FRenderQueryRHIRef Q0,Q1,D0,D1;
    ENQUEUE_RENDER_COMMAND(ToonLiveAtlas)([&A,&Q0,&Q1,&D0,&D1](FRHICommandListImmediate& R)
    {
        Q0=RHICreateRenderQuery(RQT_AbsoluteTime);Q1=RHICreateRenderQuery(RQT_AbsoluteTime);R.EndRenderQuery(Q0);
        A.Invalid=ToonProfile::GetToonProfileTextureAtlas()==nullptr;const double Start=FPlatformTime::Seconds();
        D0=RHICreateRenderQuery(RQT_AbsoluteTime);D1=RHICreateRenderQuery(RQT_AbsoluteTime);
        FRDGBuilder Graph(R);
        Graph.AddPass(RDG_EVENT_NAME("ToonLive.BeginDispatchInterval"),ERDGPassFlags::None,[D0](FRHICommandList& C){C.EndRenderQuery(D0);});
        ToonProfile::UpdateToonProfileTextureAtlas(Graph,GMaxRHIShaderPlatform);
        Graph.AddPass(RDG_EVENT_NAME("ToonLive.EndDispatchInterval"),ERDGPassFlags::None,[D1](FRHICommandList& C){C.EndRenderQuery(D1);});
        Graph.Execute();
        A.CPU=(FPlatformTime::Seconds()-Start)*1000;
        auto* Tex=ToonProfile::GetToonProfileTextureAtlas();A.Valid=Tex!=nullptr;
        if(Tex){const auto D=Tex->GetDesc();A.X=D.Extent.X;A.Y=D.Extent.Y;A.Slices=D.ArraySize;}
        R.EndRenderQuery(Q1);
    });FlushRenderingCommands();
    ENQUEUE_RENDER_COMMAND(ToonLiveQuery)([&A,Q0,Q1,D0,D1](FRHICommandListImmediate& R)
    {uint64 T0=0,T1=0;if(RHIGetRenderQueryResult(Q0,T0,true)&&RHIGetRenderQueryResult(Q1,T1,true)&&T1>=T0)A.GPU=(T1-T0)/1000.;
     if(RHIGetRenderQueryResult(D0,T0,true)&&RHIGetRenderQueryResult(D1,T1,true)&&T1>=T0)A.DispatchGPU=(T1-T0)/1000.;});
    FlushRenderingCommands();return A;
}
void Capture(FState& S,const FString& Name,bool Save=true)
{
    auto& F=*S.Base;F.Scene.GetWorld()->Tick(LEVELTICK_All,1.f/60);F.Scene.GetWorld()->SendAllEndOfFrameUpdates();F.Capture->CaptureScene();
    FlushRenderingCommands();TArray<FLinearColor> Data;
    F.Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Data,FReadSurfaceDataFlags(RCM_MinMax));
    if(!Save)return;
    FFileHelper::SaveArrayToFile(TArrayView<const uint8>((const uint8*)Data.GetData(),Data.Num()*sizeof(FLinearColor)),*(S.Out/(Name+TEXT(".rgba32f"))));
    TArray<FColor> Display;for(auto C:Data)Display.Add(FLinearColor(C.R,C.G,C.B,1).ToFColorSRGB());TArray64<uint8> PNG;
    FImageUtils::PNGCompressImageArray(Size,Size,Display,PNG);FFileHelper::SaveArrayToFile(PNG,*(S.Out/(Name+TEXT(".png"))));
}
void Upload(UTexture2D* T,float Grey)
{
    auto* Region=new FUpdateTextureRegion2D(0,0,0,0,256,1);auto* Bytes=new uint8[256*4];
    for(int32 I=0;I<256;++I){Bytes[4*I]=Bytes[4*I+1]=Bytes[4*I+2]=(uint8)FMath::RoundToInt(Grey*255);Bytes[4*I+3]=255;}
    T->UpdateTextureRegions(0,1,Region,256*4,4,Bytes,[](uint8* Data,const FUpdateTextureRegion2D* R){delete[] Data;delete R;});
}
void Persistence(FState& S)
{
    auto& F=*S.Base;auto* PP=CreatePackage(TEXT("/ToonLiveResearch/PersistProfile"));
    auto* P=DuplicateObject<UToonProfile>(F.Profiles[TEXT("LiveA")],PP,TEXT("PersistProfile"));P->SetFlags(RF_Public|RF_Standalone);P->Guid=FGuid::NewGuid();
    SetRamp(P,.35f,.12f);P->Settings.bDiffuseRampIncludeShadow=false;
    auto* MP=CreatePackage(TEXT("/ToonLiveResearch/PersistMaterial"));auto* M=DuplicateObject<UMaterial>(F.Mats[TEXT("LiveA")],MP,TEXT("PersistMaterial"));
    M->SetFlags(RF_Public|RF_Standalone);CastChecked<UMaterialExpressionSubstrateToonBSDF>(M->GetEditorOnlyData()->FrontMaterial.Expression)->ToonProfile=P;
    M->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();GShaderCompilingManager->FinishAllCompilation();M->EnsureIsComplete();
    FSavePackageArgs A;A.TopLevelFlags=RF_Public|RF_Standalone;A.SaveFlags=SAVE_NoError;
    bool SavedP=UPackage::SavePackage(PP,P,*(S.AssetDir/TEXT("PersistProfile.uasset")),A);
    bool SavedM=UPackage::SavePackage(MP,M,*(S.AssetDir/TEXT("PersistMaterial.uasset")),A);
    F.Mesh->SetStaticMesh(S.Sphere);F.Mesh->SetWorldLocation(FVector::ZeroVector);F.Mesh->SetCastShadow(false);
    F.Second->SetVisibility(false);auto* MID=F.Keep(UMaterialInstanceDynamic::Create(M,GetTransientPackage()));F.Mesh->SetMaterial(0,MID);
    for(int32 I=0;I<12;++I)Capture(S,TEXT("unused"),false);Capture(S,TEXT("PersistWritten"));
    auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("profile_saved"),SavedP);O->SetBoolField(TEXT("material_saved"),SavedM);
    O->SetNumberField(TEXT("tau"),.35);O->SetNumberField(TEXT("width"),.12);O->SetStringField(TEXT("root"),S.AssetDir);SaveJSON(O,S.Out/TEXT("persistence.json"));
}
class FFrames:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;TSharedPtr<FState> S;
public:
    FFrames(FAutomationTestBase* T,TSharedPtr<FState> State):Test(T),S(State){}
    bool Update() override
    {
        auto& F=*S->Base;
        if(S->Index>=S->Steps.Num())
        {
            auto O=MakeShared<FJsonObject>();O->SetArrayField(TEXT("steps"),S->Rows);O->SetObjectField(TEXT("warmup_stats"),S->InitialStats);
            O->SetObjectField(TEXT("final_stats"),Stats());O->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601());
            O->SetStringField(TEXT("actual_rhi_adapter"),GRHIAdapterName);O->SetStringField(TEXT("engine"),FEngineVersion::Current().ToString());
            O->SetBoolField(TEXT("reopen_process"),S->Reload);SaveJSON(O,S->Out/TEXT("updates.json"));
            if(!S->Reload)Persistence(*S);
            Test->AddInfo(FString::Printf(TEXT("Toon live %d steps; immutable production assets; outputs %s"),S->Steps.Num(),*S->Out));
            S.Reset();return true;
        }
        const FStep V=S->Steps[S->Index++];
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),V.Name);Row->SetStringField(TEXT("route"),V.Route);
        Row->SetNumberField(TEXT("tau"),V.Tau);Row->SetNumberField(TEXT("width"),V.Width);Row->SetBoolField(TEXT("smooth"),V.Smooth);
        Row->SetBoolField(TEXT("shared"),V.Shared);Row->SetBoolField(TEXT("two_slots"),V.Slots);Row->SetBoolField(TEXT("self_shadow"),V.Self);
        Row->SetBoolField(TEXT("include_shadow"),V.Inside);Row->SetBoolField(TEXT("natural_renderer_update"),V.Natural);
        Row->SetObjectField(TEXT("stats_before"),Stats());auto* M=F.Mats[V.Material];auto* MID=F.MIDs[V.Material];
        auto* Resource=M->GetMaterialResource(GMaxRHIShaderPlatform);auto* Map=Resource->GetGameThreadShaderMap();
        Row->SetStringField(TEXT("material_state_id_before"),M->StateId.ToString());
        const double BindStart=FPlatformTime::Seconds();
        F.Mesh->SetStaticMesh(V.Slots?S->SlotsMesh:S->Sphere);F.Mesh->SetWorldLocation(FVector::ZeroVector);F.Mesh->SetWorldScale3D(FVector(1));
        F.Mesh->SetCastShadow(V.Self);F.Mesh->SetMaterial(0,MID);F.Second->SetVisibility(false);
        if(V.Slots)F.Mesh->SetMaterial(1,V.Shared?F.MIDs[TEXT("SharedRight")]:S->Right);
        Row->SetNumberField(TEXT("bind_enqueue_cpu_ms"),(FPlatformTime::Seconds()-BindStart)*1000);
        FlushRenderingCommands();Row->SetNumberField(TEXT("bind_and_flush_cpu_ms"),(FPlatformTime::Seconds()-BindStart)*1000);
        const double Start=FPlatformTime::Seconds();
        int32 Notifies=0,Queues=0,Uploads=0;
        if(V.Route==TEXT("A")||V.Route==TEXT("Natural"))
        {
            for(int32 I=0;I<V.Submissions;++I){SetRamp(F.Profiles[TEXT("LiveA")],V.Tau,V.Width,V.Smooth);F.Profiles[TEXT("LiveA")]->Settings.bDiffuseRampIncludeShadow=V.Inside;Notify(F.Profiles[TEXT("LiveA")]);++Notifies;}
        }
        if(V.Route==TEXT("RuntimeAPI"))
        {
            SetRamp(F.Profiles[TEXT("LiveA")],V.Tau,V.Width,V.Smooth);
            const auto Settings=F.Profiles[TEXT("LiveA")]->Settings;auto* P=F.Profiles[TEXT("LiveA")];const auto Guid=P->Guid;
            ENQUEUE_RENDER_COMMAND(ToonLiveRawUpdate)([P,Guid,Settings](FRHICommandListImmediate& R){ToonProfile::AddOrUpdateProfile(P,Guid,Settings);});++Queues;
        }
        if(V.Route==TEXT("B"))MID->SetVectorParameterValue(TEXT("LiveUV"),FLinearColor(.5f-.5f*V.Tau,.5f,0,0));
        if(V.Route==TEXT("C"))
        {
            Upload(S->DynamicOffset,.5f-.5f*V.Tau);++Uploads;
            if(V.NotifyTexture){Notify(F.Profiles[TEXT("OffsetC")]);++Notifies;}
        }
        Row->SetNumberField(TEXT("edit_cpu_ms"),(FPlatformTime::Seconds()-Start)*1000);
        Row->SetNumberField(TEXT("notify_calls"),Notifies);Row->SetNumberField(TEXT("runtime_update_calls"),Queues);Row->SetNumberField(TEXT("texture_upload_calls"),Uploads);
        FAtlas A;
        if(!V.Natural)A=ProbeAtlas();
        Row->SetBoolField(TEXT("atlas_invalid_before_probe"),A.Invalid);Row->SetBoolField(TEXT("atlas_valid_after_probe"),A.Valid);
        Row->SetNumberField(TEXT("atlas_probe_calls"),V.Natural?0:1);Row->SetNumberField(TEXT("atlas_build_cpu_ms"),A.CPU);
        Row->SetNumberField(TEXT("atlas_gpu_ms"),A.GPU);Row->SetNumberField(TEXT("atlas_dispatch_interval_gpu_ms"),A.DispatchGPU);
        Row->SetNumberField(TEXT("atlas_x"),A.X);Row->SetNumberField(TEXT("atlas_y"),A.Y);Row->SetNumberField(TEXT("atlas_slices"),A.Slices);
        const double CaptureStart=FPlatformTime::Seconds();Capture(*S,V.Name+TEXT("_f0"));
        Row->SetNumberField(TEXT("capture_and_readback_cpu_ms"),(FPlatformTime::Seconds()-CaptureStart)*1000);
        Capture(*S,V.Name+TEXT("_f1"));
        Row->SetBoolField(TEXT("same_material_resource"),M->GetMaterialResource(GMaxRHIShaderPlatform)==Resource);
        Row->SetBoolField(TEXT("same_shader_map"),Resource->GetGameThreadShaderMap()==Map);
        Row->SetStringField(TEXT("material_state_id_after"),M->StateId.ToString());Row->SetObjectField(TEXT("stats_after"),Stats());
        Test->TestTrue(TEXT("Update preserves already compiled material resource"),M->GetMaterialResource(GMaxRHIShaderPlatform)==Resource);
        Test->TestTrue(TEXT("Update preserves already compiled ShaderMap"),Resource->GetGameThreadShaderMap()==Map);
        S->Rows.Add(MakeShared<FJsonValueObject>(Row));return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FToonLiveResearch,"NPR.Research.ToonLive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FToonLiveResearch::RunTest(const FString& Args)
{
    using namespace ToonLive;auto S=MakeShared<FState>();auto& F=*S->Base;
    FParse::Value(FCommandLine::Get(),TEXT("ToonLiveOut="),S->Out);FParse::Value(FCommandLine::Get(),TEXT("ToonLiveAssets="),S->AssetDir);
    if(!TestTrue(TEXT("Explicit output and isolated asset mount required"),!S->Out.IsEmpty()&&!S->AssetDir.IsEmpty()))return false;
    IFileManager::Get().MakeDirectory(*S->Out,true);IFileManager::Get().MakeDirectory(*S->AssetDir,true);
    FPackageName::RegisterMountPoint(TEXT("/ToonLiveResearch/"),S->AssetDir+TEXT("/"));
    S->Reload=FParse::Param(FCommandLine::Get(),TEXT("ToonLiveReload"));
    auto Cold=Stats();SaveJSON(Cold.ToSharedRef(),S->Out/TEXT("cold_stats_before.json"));
    F.Profile(TEXT("LiveA"),.5f,.08f);F.Profile(TEXT("Control"),.5f,.08f);F.Profile(TEXT("OffsetB"),0,.08f);F.Profile(TEXT("OffsetC"),0,.08f);
    auto Texture=[&](bool Gradient)
    {
        auto* T=F.Keep(UTexture2D::CreateTransient(256,1,PF_B8G8R8A8));T->SRGB=false;T->Filter=TF_Bilinear;T->NeverStream=true;
        auto& Mip=T->GetPlatformData()->Mips[0];auto* B=(uint8*)Mip.BulkData.Lock(LOCK_READ_WRITE);
        for(int32 I=0;I<256;++I){B[I*4]=B[I*4+1]=B[I*4+2]=Gradient?(uint8)I:128;B[I*4+3]=255;}Mip.BulkData.Unlock();T->UpdateResource();return T;
    };
    S->Offset=Texture(true);S->DynamicOffset=Texture(false);
    for(auto Name:{TEXT("OffsetB"),TEXT("OffsetC")})
    {
        auto* P=F.Profiles[Name];P->Settings.DiffuseRampOffsetTexture=FString(Name)==TEXT("OffsetB")?S->Offset:S->DynamicOffset;
        P->Settings.DiffuseRampOffsetStrength=.5f;auto* M=F.Mats[Name];
        auto* UV=Node<UMaterialExpressionVectorParameter>(M);UV->ParameterName=TEXT("LiveUV");UV->DefaultValue=FLinearColor(.5f,.5f,0,0);
        CastChecked<UMaterialExpressionSubstrateToonBSDF>(M->GetEditorOnlyData()->FrontMaterial.Expression)->PatternUVs.Connect(0,UV);M->PostEditChange();Notify(P);
    }
    for(int32 I=0;I<12;++I){const float T=.15f+.05f*I;const FString Name=FString::Printf(TEXT("Cache%02d"),I);F.Profile(Name,T,.08f);}
    for(int32 I=0;I<6;++I){const FString Name=FString::Printf(TEXT("Width%02d"),I);F.Profile(Name,.5f,.02f+.03f*I);}
    if(S->Reload)
    {
        auto* M=LoadObject<UMaterial>(nullptr,TEXT("/ToonLiveResearch/PersistMaterial.PersistMaterial"));
        if(!TestNotNull(TEXT("Saved research material loads in fresh process"),M))return false;
        F.Keep(M);F.Mats.Add(TEXT("Reload"),M);
    }
    FAssetCompilingManager::Get().FinishAllCompilation();GShaderCompilingManager->FinishAllCompilation();
    for(auto& E:F.Mats){E.Value->EnsureIsComplete();F.MIDs.Add(E.Key,F.Keep(UMaterialInstanceDynamic::Create(E.Value,GetTransientPackage())));}
    S->Right=F.Keep(UMaterialInstanceDynamic::Create(F.Mats[TEXT("Control")],GetTransientPackage()));
    F.MIDs.Add(TEXT("SharedRight"),F.Keep(UMaterialInstanceDynamic::Create(F.Mats[TEXT("LiveA")],GetTransientPackage())));
    S->Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    F.Mesh=F.Geometry(S->Sphere,FTransform::Identity);F.Mesh->SetMaterial(0,F.MIDs[TEXT("LiveA")]);F.Mesh->SetCastShadow(false);
    F.Second=F.Geometry(S->Sphere,FTransform::Identity);F.Second->SetVisibility(false);
    auto* D=F.Keep(UStaticMesh::CreateStaticMeshDescription());
    for(int32 I=0;I<2;++I)
    {
        auto G=D->CreatePolygonGroup();D->SetPolygonGroupMaterialSlotName(G,FName(*FString::Printf(TEXT("Slot%d"),I)));
        int32 First=D->GetMeshDescription().Vertices().Num();FPolygonID P[6];D->CreateCube(FVector::ZeroVector,FVector(25,25,35),G,P[0],P[1],P[2],P[3],P[4],P[5]);
        auto Positions=D->GetVertexPositions();for(auto V:D->GetMeshDescription().Vertices().GetElementIDs())if(V.GetValue()>=First)Positions[V]+=FVector3f(0,I?35.f:-35.f,0);
    }
    S->SlotsMesh=F.Keep(NewObject<UStaticMesh>());S->SlotsMesh->GetStaticMaterials().Add(FStaticMaterial(F.Mats[TEXT("LiveA")],TEXT("Slot0"),TEXT("Slot0")));
    S->SlotsMesh->GetStaticMaterials().Add(FStaticMaterial(F.Mats[TEXT("Control")],TEXT("Slot1"),TEXT("Slot1")));S->SlotsMesh->BuildFromStaticMeshDescriptions({D},false,true);
    F.Directional=NewObject<UDirectionalLightComponent>();F.Directional->SetMobility(EComponentMobility::Movable);F.Directional->SetIntensity(PI);
    F.Directional->bTransmission=true;F.Scene.AddComponent(F.Directional,FTransform(FRotator(0,55,0)));
    F.Capture=NewObject<USceneCaptureComponent2D>();F.Capture->FOVAngle=40;F.Capture->bCaptureEveryFrame=false;F.Capture->bCaptureOnMovement=false;F.Capture->bAlwaysPersistRenderingState=true;
    F.Capture->ShowFlags.SetTemporalAA(false);F.Capture->ShowFlags.SetBloom(false);F.Capture->ShowFlags.SetMotionBlur(false);F.Capture->ShowFlags.SetGlobalIllumination(false);
    F.Capture->ShowFlags.SetReflectionEnvironment(false);F.Capture->ShowFlags.SetSkyLighting(false);F.Capture->CaptureSource=SCS_SceneColorHDR;
    auto& PP=F.Capture->PostProcessSettings;PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=AEM_Manual;PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_DynamicGlobalIlluminationMethod=true;PP.DynamicGlobalIlluminationMethod=EDynamicGlobalIlluminationMethod::None;
    F.Target=F.Keep(NewObject<UTextureRenderTarget2D>());F.Target->RenderTargetFormat=RTF_RGBA16f;F.Target->InitAutoFormat(Size,Size);F.Target->UpdateResourceImmediate(true);F.Capture->TextureTarget=F.Target;
    F.Scene.AddComponent(F.Capture,FTransform(FRotator::ZeroRotator,FVector(-230,0,0)));
    for(int32 I=0;I<16;++I)Capture(*S,TEXT("unused"),false);
    F.Capture->CaptureSource=SCS_BaseColor;Capture(*S,TEXT("Mask"));F.Capture->CaptureSource=SCS_Normal;Capture(*S,TEXT("Normals"));F.Capture->CaptureSource=SCS_SceneColorHDR;
    ProbeAtlas();S->InitialStats=Stats();SaveJSON(S->InitialStats.ToSharedRef(),S->Out/TEXT("cold_stats_after.json"));
    auto Add=[&](FString Name,FString Route,float Tau,float W=.08f)->FStep&{FStep V;V.Name=Name;V.Route=Route;V.Tau=Tau;V.Width=W;return S->Steps.Add_GetRef(V);};
    if(S->Reload)Add(TEXT("Reload"),TEXT("D"),.35f,.12f).Material=TEXT("Reload");
    else
    {
        for(int32 I=0;I<12;++I)Add(FString::Printf(TEXT("Baseline%02d"),I),TEXT("Hold"),.5f);
        for(auto Route:{TEXT("A"),TEXT("D"),TEXT("B"),TEXT("C"),TEXT("Natural"),TEXT("RuntimeAPI")})
            for(int32 I=0;I<12;++I)
            {
                auto& V=Add(FString::Printf(TEXT("%s_T%02d"),Route,I),Route,.15f+.05f*I);
                if(FString(Route)==TEXT("D"))V.Material=FString::Printf(TEXT("Cache%02d"),I);
                if(FString(Route)==TEXT("B"))V.Material=TEXT("OffsetB");if(FString(Route)==TEXT("C"))V.Material=TEXT("OffsetC");
                V.Natural=FString(Route)==TEXT("Natural");
            }
        for(auto Route:{TEXT("A"),TEXT("D")})for(int32 I=0;I<6;++I)
        {auto& V=Add(FString::Printf(TEXT("%s_W%02d"),Route,I),Route,.5f,.02f+.03f*I);if(FString(Route)==TEXT("D"))V.Material=FString::Printf(TEXT("Width%02d"),I);}
        Add(TEXT("C_Stale"),TEXT("C"),-.3f).Material=TEXT("OffsetC");S->Steps.Last().NotifyTexture=false;
        Add(TEXT("C_Refresh"),TEXT("C"),-.3f).Material=TEXT("OffsetC");
        auto& Before=Add(TEXT("SlotsBefore"),TEXT("A"),.2f);Before.Slots=true;
        auto& After=Add(TEXT("SlotsAfter"),TEXT("A"),.8f);After.Slots=true;
        auto& Shared0=Add(TEXT("SharedBefore"),TEXT("A"),.2f);Shared0.Slots=true;Shared0.Shared=true;
        auto& Shared1=Add(TEXT("SharedAfter"),TEXT("A"),.8f);Shared1.Slots=true;Shared1.Shared=true;
        Add(TEXT("NegativeSelf"),TEXT("A"),-.5f).Self=true;Add(TEXT("NegativeClear"),TEXT("A"),-.5f);
        auto& Inside=Add(TEXT("InsideSelf"),TEXT("A"),-.5f);Inside.Self=true;Inside.Inside=true;
        Add(TEXT("Hermite"),TEXT("A"),.5f,.15f).Smooth=true;
        Add(TEXT("ZeroWidth"),TEXT("A"),.5f,0);
        Add(TEXT("Boundary"),TEXT("A"),.95f,.2f);
        Add(TEXT("Coalesced10"),TEXT("A"),.4f).Submissions=10;
    }
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<ToonLive::FFrames>(this,S));return true;
}
#endif
