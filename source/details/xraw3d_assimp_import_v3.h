#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "dependencies/xstrtool/source/xstrtool.h"
#include "dependencies/xerr/source/xerr.h"
#include <functional>
#include <string>
#include <vector>


#pragma message("***NOTE*** Import3d.h is adding to the program the assimp-vc143-mt.lib library")
#pragma comment( lib, "dependencies/assimp/BINARIES/Win32/lib/Release/assimp-vc143-mt.lib")

//---------------------------------------------------------------------------------------------------------
// THE IMPORTER
//---------------------------------------------------------------------------------------------------------
// The importer normalizes root transforms in bind pose(neutral / skin / invBind) to align skeletons at origin(0, 0, 0) for cross - FBX compatibility,
// while preserving original visuals by compensating in invBind and skin vertices.Keys remain untouched to avoid distorting motion.
//
// Skeleton(Neutral & InvBind) : Accumulates node transforms for neutrals(local - to - world in bind), computes invBind from offset* inverse mesh global.
// Normalization multiplies invBind by rootBind(shifting to root - local).Why good : Neutrals stay original for animation deltas;
// compatibility ensures matching - hierarchy FBX share root space(e.g., swap animations without offsets).
// Potential issues : Assumes root is always index 0 or findable-multi - root scenes could fail.Non-uniform scales in hierarchy may distort if not decomposed properly.
// Skin(Vertices & Weights) : Transforms verts to world(MeshGlobal), then root - local(invRootBind * MeshGlobal); rotates vectors via decomposition(ignores scale to avoid shear).
// Weights packed as uint8(normalized / sorted). Why good : Aligns mesh to normalized skeleton for portability; full skinning in bbox fix ensures
// accurate extents / scaling.Potential issues : GLTF / FBX unit differences(e.g., cm vs.m) cause scale mismatches-add aiProcess_GlobalScale to ReadFile
// flags.Multi - reference non - skinned meshes duplicate inefficiently for large models.
// Animation(Keys) : Samples / interpolates channel keys per frame(linear pos / scale, slerp rot); fills static bones from node decomps. No normalization.
// Why good : Preserves exact deltas for identical playback; root motion intact as keys apply on original neutrals. Compatibility via normalized
// bind-animations from other FBX work if bones match. Potential issues : High FPS sampling bloats memory; no compression for future add key reduction.
namespace xraw3d::assimp_v3
{
    enum state : std::uint8_t
    { OK
    , FAILURE
    , ASSIMP_FAIL_TO_IMPORT
    , WARNING
    };

    struct node
    {
        std::string                 m_Name;
        std::vector<node>           m_Children;
        std::vector<std::uint32_t>  m_MeshList;
    };

    template< typename T>
    struct scope_exit
    {
        explicit scope_exit(T&& func) : m_Func(std::forward<decltype(func)>(func)) {}
        ~scope_exit() { m_Func(); }
        T m_Func;
    };

    struct importer
    {
        using anim_list = std::vector<xraw3d::anim>;

        struct settings
        {
            bool            m_bStaticGeometry           = true;
            bool            m_bLimitBoneWeights         = true;
            bool            m_bGenerateTBNInformation   = true;
            bool            m_bFlipUVs                  = true;
            bool            m_bGenerateNormals          = true;
            node*           m_pNode                     = nullptr;      // Collect the hirarchy
            xraw3d::geom*   m_pGeom                     = nullptr;      // Import geometry
            anim_list*      m_pAnims                    = nullptr;      // Import all anims
            xraw3d::anim*   m_pSkeleton                 = nullptr;      // Import the skeleton only
        };

        xerr Import( std::wstring_view FileName, settings& Settings ) noexcept
        {
            scope_exit CleanUp([&]
            {
                //
                // Set everything back to null
                //
                m_pAnims    = nullptr;
                m_pGeom     = nullptr;
                m_pScene    = nullptr;
                m_pSkeleton = nullptr;
            });

            auto Importer = std::make_unique<Assimp::Importer>();
           
            m_pAnims    = Settings.m_pAnims;
            m_pGeom     = Settings.m_pGeom;
            m_pSkeleton = Settings.m_pSkeleton;
            m_bStaticGeom = Settings.m_bStaticGeometry;

            if(m_pGeom)
            {
                //m_pGeom->clear();
            }

            if(m_pAnims)
            {
                m_pAnims->clear();
            }

            if (m_pSkeleton)
            {
                //m_pSkeleton->clear();
            }

            m_pScene = Importer->ReadFile
            ( xstrtool::To(FileName).c_str()
            , aiProcess_Triangulate                 // Make sure we get triangles rather than nvert polygons
            | (Settings.m_bLimitBoneWeights       ? aiProcess_LimitBoneWeights : 0)       // 4 weights for skin model max
            | aiProcess_GenUVCoords                 // Convert any type of mapping to uv mapping
            | aiProcess_TransformUVCoords           // preprocess UV transformations (scaling, translation ...)
            | aiProcess_FindInstances               // search for instanced meshes and remove them by references to one master
            | (Settings.m_bGenerateNormals        ? aiProcess_GenNormals       : 0)       // if it does not have normals generate them... (this may not be a good option as it may hide issues from artist)
            | (Settings.m_bGenerateTBNInformation ? aiProcess_CalcTangentSpace : 0)       // calculate tangents and bitangents if possible (definetly you will meed UVs)
                                                    // | aiProcess_JoinIdenticalVertices // join identical vertices/ optimize indexing (It seems to be creating cracks in the mesh... some bug?)
            | aiProcess_RemoveRedundantMaterials    // remove redundant materials
            | aiProcess_FindInvalidData             // detect invalid model data, such as invalid normal vectors
            | (Settings.m_bFlipUVs ? aiProcess_FlipUVs : 0)   // flip the V to match the Vulkans way of doing UVs
            );

            if( m_pScene == nullptr ) return xerr::create<state::ASSIMP_FAIL_TO_IMPORT, "Assimp failed to load a file">();
            if( auto Err = SanityCheck(); Err ) return Err;

            if( not m_bStaticGeom && (Settings.m_pGeom || Settings.m_pSkeleton || Settings.m_pAnims))
            {
                ImportSkeleton();
            }

            if(Settings.m_pAnims)
            {
                ImportAnimations();
            }

            if(Settings.m_pGeom)
            {
                ImportGeometry();
                ImportMaterials();
            }

            if(Settings.m_pNode)
            {
                *Settings.m_pNode = buildNodes(m_pScene->mRootNode, m_pScene);
            }

            return {};
        }

        //------------------------------------------------------------------------------------------------------
        // TYPES
        //------------------------------------------------------------------------------------------------------
    protected:

        struct refs
        {
            std::vector<const aiNode*>      m_Nodes;
        };

        struct myMeshPart
        {
            std::string                         m_MeshName;
            //std::string                         m_Name;
            std::string                         m_FullMeshName;
            std::vector<xraw3d::geom::vertex>   m_Vertices;
            std::vector<int>                    m_Indices;
            int                                 m_iMaterialInstance;
        };

        struct internal_bone
        {
            std::string                         name;
            int                                 parent = -1;
            int                                 nChildren = 0;
            xmath::fvec3                        scale{ 1,1,1 };
            xmath::fquat                        rot{ 0,0,0,1 };
            xmath::fvec3                        pos{ 0,0,0 };
            xmath::fmat4                        invBind;
            xmath::fmat4                        neutral;
        };

        //------------------------------------------------------------------------------------------------------
        // FUNCTIONS
        //------------------------------------------------------------------------------------------------------
    protected:

        //--------------------------------------------------------------------------------------------------

        node buildNodes(const aiNode* assimpNode, const aiScene* scene)
        {
            node n;
            n.m_Name = assimpNode->mName.C_Str();

            // Collect which meshes are used in this node
            for (unsigned int i = 0; i < assimpNode->mNumMeshes; ++i)
            {
                n.m_MeshList.push_back(assimpNode->mMeshes[i]);
            }

            // Recurse children
            for (unsigned int k = 0; k < assimpNode->mNumChildren; ++k)
            {
                n.m_Children.push_back(buildNodes(assimpNode->mChildren[k], scene));
            }

            return n;
        }

        //--------------------------------------------------------------------------------------------------

        xerr SanityCheck() noexcept
        {
            m_MeshReferences.resize(m_pScene->mNumMeshes);
            std::function<void(const aiNode& Node)> ProcessNode = [&](const aiNode& Node) noexcept
            {
                for (auto i = 0u, end = Node.mNumMeshes; i < end; ++i)
                {
                    aiMesh* pMesh = m_pScene->mMeshes[Node.mMeshes[i]];
                    m_MeshReferences[Node.mMeshes[i]].m_Nodes.push_back(&Node);
                }
                for (auto i = 0u; i < Node.mNumChildren; ++i)
                {
                    ProcessNode(*Node.mChildren[i]);
                }
            };

            ProcessNode( *m_pScene->mRootNode );

            for (auto iMesh = 0u; iMesh < m_pScene->mNumMeshes; ++iMesh)
            {
                const aiMesh&   AssimpMesh  = *m_pScene->mMeshes[iMesh];
                const auto&     Refs        = m_MeshReferences[iMesh].m_Nodes;

                if (Refs.size() == 0u)
                {
                    xerr::LogMessage<state::WARNING>( std::format("WARNING: I had a mesh [{}] but no reference to it in the scene... very strange", AssimpMesh.mName.C_Str()) );
                }

                if(AssimpMesh.HasBones())
                {
                    if (Refs.size() > 1 )
                    {
                        xerr::LogMessage<state::FAILURE>( std::format("I had a skin mesh {} that is reference in the scene {} times. We don't support this feature.", AssimpMesh.mName.C_Str(), Refs.size()));
                        return xerr::create_f<state, "Multiple skin meshes in the scene... we only support one">();
                    }
                }
                else
                {
                    if (Refs.size() > 1)
                    {
                        printf("INFO: I will be duplicating mesh %s, %zd times\n", AssimpMesh.mName.C_Str(), Refs.size());
                    }
                }
            }

            return {};
        }

        //------------------------------------------------------------------------------------------------------
        std::string GetMeshNameFromNode(const aiNode& Node)
        {
            for( auto pNode = &Node; pNode; pNode = pNode->mParent )
            {
                // Using the naming convention to group meshes...
                if (xstrtool::findI(pNode->mName.C_Str(), "MESH_") != std::string::npos)
                {
                    return pNode->mName.C_Str();
                }
            }
            return {};
        }

        //------------------------------------------------------------------------------------------------------
        void ImportMaterials() noexcept
        {
            std::unordered_map<int,int> AssimMaterialToGeomMaterial;
            std::unordered_set<int>     UsedMaterials;

            for(const auto& part : m_MyMeshParts)
            {
                UsedMaterials.insert(part.m_iMaterialInstance);
            }

            int newIndex = 0;
            for(int matIndex : UsedMaterials)
            {
                AssimMaterialToGeomMaterial[matIndex] = newIndex++;
                auto    pcMat   = m_pScene->mMaterials[matIndex];
                auto&   MatI    = m_pGeom->m_MaterialInstance.emplace_back();
                MatI.m_Name = pcMat->GetName().C_Str();

                //
                // Shading model
                //
                {
                    int ShadingModel = -1;
                    aiGetMaterialInteger(pcMat, AI_MATKEY_SHADING_MODEL, (int*)&ShadingModel);
                    switch (ShadingModel)
                    {
                        case aiShadingMode_Gouraud:
                        case aiShadingMode_Flat:
                        case aiShadingMode_Phong:
                        case aiShadingMode_Blinn:       MatI.m_MaterialShader = "Gouraud"; break;
                        case aiShadingMode_Toon:        MatI.m_MaterialShader = "Toon"; break;
                        case aiShadingMode_NoShading:   MatI.m_MaterialShader = "Unlit"; break;
                        case aiShadingMode_OrenNayar:
                        case aiShadingMode_Minnaert:
                        case aiShadingMode_Fresnel:
                        case aiShadingMode_CookTorrance:
                        case aiShadingMode_PBR_BRDF:    MatI.m_MaterialShader = "PBR"; break;
                        default: MatI.m_MaterialShader = "Unknown"; break;
                    }
                }
                MatI.m_Technique = "";

                //
                // Diffuse Color
                //
                {
                    aiColor4D C(1, 1, 1, 1);
                    aiGetMaterialColor(pcMat, AI_MATKEY_COLOR_DIFFUSE, (aiColor4D*)&C);
                    geom::material_instance::params p;
                    p.m_Type    = geom::material_instance::params_type::F4;
                    p.m_Name    = "DiffuseColor";
                    p.m_Value   = std::to_string(C.r) + "," + std::to_string(C.g) + "," + std::to_string(C.b) + "," + std::to_string(C.a);
                    MatI.m_Params.push_back(p);
                }

                //
                // Specular Color
                //
                {
                    aiColor4D C(0, 0, 0, 1);
                    aiGetMaterialColor(pcMat, AI_MATKEY_COLOR_SPECULAR, (aiColor4D*)&C);
                    geom::material_instance::params p;
                    p.m_Type    = geom::material_instance::params_type::F4;
                    p.m_Name    = "SpecularColor";
                    p.m_Value   = std::to_string(C.r) + "," + std::to_string(C.g) + "," + std::to_string(C.b) + "," + std::to_string(C.a);
                    MatI.m_Params.push_back(p);
                }

                //
                // Ambient Color
                //
                {
                    aiColor4D C(0, 0, 0, 1);
                    aiGetMaterialColor(pcMat, AI_MATKEY_COLOR_AMBIENT, (aiColor4D*)&C);
                    geom::material_instance::params p;
                    p.m_Type    = geom::material_instance::params_type::F4;
                    p.m_Name    = "AmbientColor";
                    p.m_Value   = std::to_string(C.r) + "," + std::to_string(C.g) + "," + std::to_string(C.b) + "," + std::to_string(C.a);
                    MatI.m_Params.push_back(p);
                }

                //
                // Emissive Color
                //
                {
                    aiColor4D C(0, 0, 0, 1);
                    aiGetMaterialColor(pcMat, AI_MATKEY_COLOR_EMISSIVE, (aiColor4D*)&C);
                    geom::material_instance::params p;
                    p.m_Type    = geom::material_instance::params_type::F4;
                    p.m_Name    = "EmissiveColor";
                    p.m_Value   = std::to_string(C.r) + "," + std::to_string(C.g) + "," + std::to_string(C.b) + "," + std::to_string(C.a);
                    MatI.m_Params.push_back(p);
                }

                //
                // Opacity float
                //
                {
                    float f = 1;
                    aiGetMaterialFloat(pcMat, AI_MATKEY_OPACITY, &f);
                    geom::material_instance::params p;
                    p.m_Type    = geom::material_instance::params_type::F1;
                    p.m_Name    = "OpacityFactor";
                    p.m_Value   = std::to_string(f);
                    MatI.m_Params.push_back(p);
                }

                //
                // Shininess float
                //
                {
                    float f = 0;
                    aiGetMaterialFloat(pcMat, AI_MATKEY_SHININESS, &f);
                    geom::material_instance::params p;
                    p.m_Type    = geom::material_instance::params_type::F1;
                    p.m_Name    = "ShininessFactor";
                    p.m_Value   = std::to_string(f);
                    MatI.m_Params.push_back(p);
                }

                //
                // Shininess strength float
                //
                {
                    float f = 0;
                    aiGetMaterialFloat(pcMat, AI_MATKEY_SHININESS_STRENGTH, &f);
                    geom::material_instance::params p;
                    p.m_Type    = geom::material_instance::params_type::F1;
                    p.m_Name    = "ShininessStrengthFactor";
                    p.m_Value   = std::to_string(f);
                    MatI.m_Params.push_back(p);
                }

                //
                // Diffuse Texture
                //
                {
                    aiString szPath;
                    if (AI_SUCCESS == aiGetMaterialTexture(pcMat, aiTextureType_DIFFUSE, 0, &szPath))
                    {
                        geom::material_instance::params p;
                        p.m_Type = geom::material_instance::params_type::TEXTURE;
                        p.m_Name = "DiffuseMap";
                        p.m_Value = szPath.C_Str();
                        MatI.m_Params.push_back(p);
                    }
                    else
                    {
                        for (std::uint32_t i = 0; i < pcMat->mNumProperties; ++i)
                        {
                            const auto& Props = *pcMat->mProperties[i];
                            if (Props.mType == aiPTI_String)
                            {
                                if (Props.mSemantic != aiTextureType_NONE)
                                {
                                    if(Props.mSemantic == aiTextureType_BASE_COLOR)
                                    {
                                        geom::material_instance::params p;
                                        p.m_Type = geom::material_instance::params_type::TEXTURE;
                                        p.m_Name = "DiffuseMap";
                                        p.m_Value = ((aiString*)Props.mData)->C_Str();
                                        MatI.m_Params.push_back(p);
                                        break;
                                    }
                                    else if (Props.mSemantic == aiTextureType_UNKNOWN)
                                    {
                                        if (xstrtool::findI(((aiString*)Props.mData)->C_Str(), "_Base_Color") != std::string::npos)
                                        {
                                            geom::material_instance::params p;
                                            p.m_Type = geom::material_instance::params_type::TEXTURE;
                                            p.m_Name = "DiffuseMap";
                                            p.m_Value = ((aiString*)Props.mData)->C_Str();
                                            MatI.m_Params.push_back(p);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                //
                // Specular Texture
                //
                {
                    aiString szPath;
                    if (AI_SUCCESS == aiGetMaterialTexture(pcMat, aiTextureType_SPECULAR, 0, &szPath))
                    {
                        geom::material_instance::params p;
                        p.m_Type    = geom::material_instance::params_type::TEXTURE;
                        p.m_Name    = "SpecularMap";
                        p.m_Value   = szPath.C_Str();
                        MatI.m_Params.push_back(p);
                    }
                }

                //
                // Opacity Texture
                //
                {
                    aiString szPath;
                    if (AI_SUCCESS == aiGetMaterialTexture(pcMat, aiTextureType_OPACITY, 0, &szPath))
                    {
                        geom::material_instance::params p;
                        p.m_Type    = geom::material_instance::params_type::TEXTURE;
                        p.m_Name    = "OpacityMap";
                        p.m_Value   = szPath.C_Str();
                        MatI.m_Params.push_back(p);
                    }
                    else
                    {
                        aiString diffusePath;
                        aiGetMaterialTexture(pcMat, aiTextureType_DIFFUSE, 0, &diffusePath);
                        int flags = 0;
                        aiGetMaterialInteger(pcMat, AI_MATKEY_TEXFLAGS(aiTextureType_DIFFUSE, 0), &flags);
                        if(strlen(diffusePath.data) > 0 && !(flags & aiTextureFlags_IgnoreAlpha))
                        {
                            geom::material_instance::params p;
                            p.m_Type    = geom::material_instance::params_type::TEXTURE;
                            p.m_Name    = "OpacityMap";
                            p.m_Value   = diffusePath.C_Str();
                            MatI.m_Params.push_back(p);
                        }
                    }
                }

                //
                // Ambient Texture
                //
                {
                    aiString szPath;
                    if (AI_SUCCESS == aiGetMaterialTexture(pcMat, aiTextureType_AMBIENT, 0, &szPath))
                    {
                        geom::material_instance::params p;
                        p.m_Type = geom::material_instance::params_type::TEXTURE;
                        p.m_Name = "AmbientMap";
                        p.m_Value = szPath.C_Str();
                        MatI.m_Params.push_back(p);
                    }
                }

                //
                // Emissive Texture
                //
                {
                    aiString szPath;
                    if (AI_SUCCESS == aiGetMaterialTexture(pcMat, aiTextureType_EMISSIVE, 0, &szPath))
                    {
                        geom::material_instance::params p;
                        p.m_Type    = geom::material_instance::params_type::TEXTURE;
                        p.m_Name    = "EmissiveMap";
                        p.m_Value   = szPath.C_Str();
                        MatI.m_Params.push_back(p);
                    }
                }

                //
                // Shininess Texture
                //
                {
                    aiString szPath;
                    if (AI_SUCCESS == aiGetMaterialTexture(pcMat, aiTextureType_SHININESS, 0, &szPath))
                    {
                        geom::material_instance::params p;
                        p.m_Type    = geom::material_instance::params_type::TEXTURE;
                        p.m_Name    = "ShininessMap";
                        p.m_Value   = szPath.C_Str();
                        MatI.m_Params.push_back(p);
                    }
                }

                //
                // Lightmap Texture
                //
                {
                    aiString szPath;
                    if (AI_SUCCESS == aiGetMaterialTexture(pcMat, aiTextureType_LIGHTMAP, 0, &szPath))
                    {
                        geom::material_instance::params p;
                        p.m_Type    = geom::material_instance::params_type::TEXTURE;
                        p.m_Name    = "LightmapMap";
                        p.m_Value   = szPath.C_Str();
                        MatI.m_Params.push_back(p);
                    }
                }

                //
                // Normal Texture
                //
                {
                    aiString szPath;
                    if (AI_SUCCESS == aiGetMaterialTexture(pcMat, aiTextureType_NORMALS, 0, &szPath))
                    {
                        geom::material_instance::params p;
                        p.m_Type    = geom::material_instance::params_type::TEXTURE;
                        p.m_Name    = "NormalMap";
                        p.m_Value   = szPath.C_Str();
                        MatI.m_Params.push_back(p);
                    }
                }

                //
                // Height Texture
                //
                {
                    aiString szPath;
                    if (AI_SUCCESS == aiGetMaterialTexture(pcMat, aiTextureType_HEIGHT, 0, &szPath))
                    {
                        geom::material_instance::params p;
                        p.m_Type    = geom::material_instance::params_type::TEXTURE;
                        p.m_Name    = "HeightMap";
                        p.m_Value   = szPath.C_Str();
                        MatI.m_Params.push_back(p);
                    }
                }
            }

            //
            // Remap facet materials
            //
            for(auto& f : m_pGeom->m_Facet)
            {
                auto it = AssimMaterialToGeomMaterial.find(f.m_iMaterialInstance);
                if(it != AssimMaterialToGeomMaterial.end())
                {
                    f.m_iMaterialInstance = it->second;
                }
                else
                {
                    f.m_iMaterialInstance = -1; // Invalid
                }
            }
        }

        //------------------------------------------------------------------------------------------------------

        bool ImportGeometryValidateMesh(const aiMesh& AssimpMesh, int& iTexture )
        {
            if (AssimpMesh.HasPositions() == false)
            {
                printf("WARNING: Found a mesh (%s) without position! mesh will be removed\n", AssimpMesh.mName.C_Str());
                return true;
            }

            if (AssimpMesh.HasFaces() == false)
            {
                printf("WARNING: Found a mesh (%s) without position! mesh will be removed\n", AssimpMesh.mName.C_Str());
                return true;
            }

            if (AssimpMesh.HasNormals() == false)
            {
                printf("WARNING: Found a mesh (%s) without normals! mesh will be removed\n", AssimpMesh.mName.C_Str());
                return true;
            }

            if (AssimpMesh.HasTangentsAndBitangents() == false)
            {
                printf("WARNING: Found a mesh (%s) without Tangets! We will create fake tangets.. but it will look bad!\n", AssimpMesh.mName.C_Str());
            }

            if (AssimpMesh.GetNumUVChannels() != 1)
            {
                if (AssimpMesh.GetNumUVChannels() == 0)
                {
                    printf("WARNING: Found a mesh (%s) without UVs we will assign 0,0 to all uvs\n", AssimpMesh.mName.C_Str());
                }
                else if (AssimpMesh.GetNumUVChannels() > geom::vertex_max_uv_v )
                {
                    printf("WARNING: Found a mesh (%s) without too many UV chanels we will use only one...\n", AssimpMesh.mName.C_Str());
                }
            }

            iTexture = [&]()->int
            {
                for (auto i = 0u; i < AssimpMesh.GetNumUVChannels(); ++i)
                    if (AssimpMesh.HasTextureCoords(i)) return i;

                return -1;
            }();

            return false;
        }

        //------------------------------------------------------------------------------------------------------

        void ImportGeometryStatic(std::vector<myMeshPart>& MyNodes)
        {
            auto ProcessMesh = [&](const aiMesh& AssimpMesh, const aiMatrix4x4& Transform, myMeshPart& MeshPart, const int iTexCordinates )
            {
                // get the rotation for the normals
                aiQuaternion presentRotation;
                {
                    aiVector3D p;
                    Transform.DecomposeNoScaling(presentRotation, p);
                }

                MeshPart.m_MeshName             = AssimpMesh.mName.C_Str();
                MeshPart.m_iMaterialInstance    = AssimpMesh.mMaterialIndex;
                MeshPart.m_Vertices.resize(AssimpMesh.mNumVertices);
                for (auto i = 0u; i < AssimpMesh.mNumVertices; ++i)
                {
                    xraw3d::geom::vertex&   Vertex  = MeshPart.m_Vertices[i];
                    auto                    L       = Transform * AssimpMesh.mVertices[i];
                    Vertex.m_Position.setup(static_cast<float>(L.x), static_cast<float>(L.y), static_cast<float>(L.z));

                    // Collect multiple UVs
                    const auto numUVChannels = static_cast<int>(AssimpMesh.GetNumUVChannels());
                    Vertex.m_nUVs = 0;
                    for (int ch = 0; ch < numUVChannels && Vertex.m_nUVs < geom::vertex_max_uv_v; ++ch)
                    {
                        if (AssimpMesh.HasTextureCoords(ch))
                        {
                            aiVector3D uv = AssimpMesh.mTextureCoords[ch][i];
                            Vertex.m_UV[Vertex.m_nUVs++] = xmath::fvec2(uv.x, uv.y);
                        }
                    }

                    // Collect multiple colors
                    const auto numColorChannels = static_cast<int>(AssimpMesh.GetNumColorChannels());
                    Vertex.m_nColors = 0;
                    for (int ch = 0; ch < numColorChannels && Vertex.m_nColors < geom::vertex_max_colors_v; ++ch)
                    {
                        if (AssimpMesh.HasVertexColors(ch))
                        {
                            aiColor4D c = AssimpMesh.mColors[ch][i];
                            Vertex.m_Color[Vertex.m_nColors++].setupFromRGBA(c.r, c.g, c.b, c.a);
                        }
                    }

                    if (AssimpMesh.HasTangentsAndBitangents())
                    {
                        assert(AssimpMesh.HasNormals());
                        const auto T = presentRotation.Rotate(AssimpMesh.mTangents[i]);
                        const auto B = presentRotation.Rotate(AssimpMesh.mBitangents[i]);
                        const auto N = presentRotation.Rotate(AssimpMesh.mNormals[i]);
                        Vertex.m_nNormals   = 1;
                        Vertex.m_nTangents  = 1;
                        Vertex.m_nBinormals = 1;
                        Vertex.m_BTN[0].m_Normal.setup(N.x, N.y, N.z);
                        Vertex.m_BTN[0].m_Tangent.setup(T.x, T.y, T.z);
                        Vertex.m_BTN[0].m_Binormal.setup(B.x, B.y, B.z);
                        Vertex.m_BTN[0].m_Normal.NormalizeSafe();
                        Vertex.m_BTN[0].m_Tangent.NormalizeSafe();
                        Vertex.m_BTN[0].m_Binormal.NormalizeSafe();
                    }
                    else
                    {
                        const auto N = presentRotation.Rotate(AssimpMesh.mNormals[i]);
                        Vertex.m_nNormals   = 1;
                        Vertex.m_nTangents  = 1;
                        Vertex.m_nBinormals = 1;
                        Vertex.m_BTN[0].m_Normal.setup(N.x, N.y, N.z);
                        Vertex.m_BTN[0].m_Tangent.setup(1, 0, 0);
                        Vertex.m_BTN[0].m_Binormal.setup(0, 1, 0);
                        Vertex.m_BTN[0].m_Normal.NormalizeSafe();
                    }

                    // This is a static geometry so this is kind of meaning less
                    Vertex.m_nWeights   = 0;
                    Vertex.m_iFrame     = 0;
                }

                //
                // Copy the indices
                //
                for (auto i = 0u; i < AssimpMesh.mNumFaces; ++i)
                {
                    const auto& Face = AssimpMesh.mFaces[i];
                    for (auto j = 0u; j < Face.mNumIndices; ++j)
                        MeshPart.m_Indices.push_back(Face.mIndices[j]);
                }
            };

            std::vector<std::string> Path;
            std::function<void(const aiNode&, const aiMatrix4x4&)> RecurseScene = [&]( const aiNode& Node, const aiMatrix4x4& ParentTransform)
            {
                if (m_pScene->mRootNode != &Node) Path.push_back( std::format("{}/{}", Path.back(), Node.mName.C_Str()));

                const aiMatrix4x4   Transform   = ParentTransform * Node.mTransformation;
                auto                iBase       = MyNodes.size();

                // Collect all the meshes
                MyNodes.resize(iBase + Node.mNumMeshes);
                auto currentIndex = iBase;
                for (auto i = 0u, end = Node.mNumMeshes; i < end; ++i)
                {
                    aiMesh& AssimpMesh = *m_pScene->mMeshes[Node.mMeshes[i]];
                    int iTexCordinates;
                    if( ImportGeometryValidateMesh( AssimpMesh, iTexCordinates) ) continue;
                    ProcessMesh(AssimpMesh, Transform, MyNodes[currentIndex], iTexCordinates );

                    // Set the name of the mesh
                    MyNodes[currentIndex].m_FullMeshName = std::format( "{}/{}", Path.back(), AssimpMesh.mName.C_Str() );
                    currentIndex++;
                }

                // Resize if some were skipped
                MyNodes.resize(currentIndex);

                // Do the children
                for (auto i = 0u; i < Node.mNumChildren; ++i)
                {
                    RecurseScene(*Node.mChildren[i], Transform);
                }

                Path.pop_back();
            };

            aiMatrix4x4 L2W;
            Path.push_back(m_pScene->mRootNode->mName.C_Str());
            RecurseScene( *m_pScene->mRootNode, L2W );
        }

        //------------------------------------------------------------------------------------------------------

        void ImportGeometry()
        {
            m_MyMeshParts.clear();

            //
            // Import from scene
            //
            if(m_bStaticGeom)
            {
                ImportGeometryStatic(m_MyMeshParts);
            }
            else
            {
                ImportGeometrySkin(m_MyMeshParts);
            }

            //
            // Remove Mesh parts with zero vertices
            //
            for (auto i = 0u; i < m_MyMeshParts.size(); ++i)
            {
                if (m_MyMeshParts[i].m_Vertices.size() == 0 || m_MyMeshParts[i].m_Indices.size() == 0)
                {
                    m_MyMeshParts.erase(m_MyMeshParts.begin() + i);
                    --i;
                }
            }

            //
            // Merge any mesh part based on Mesh and iMaterial...
            //
            if (false)
            for (auto i = 0u; i < m_MyMeshParts.size(); ++i)
            {
                for (auto j = i + 1; j < m_MyMeshParts.size(); ++j)
                {
                    // Lets find a candidate to merge...
                    if (m_MyMeshParts[i].m_iMaterialInstance == m_MyMeshParts[j].m_iMaterialInstance
                        && m_MyMeshParts[i].m_MeshName == m_MyMeshParts[j].m_MeshName)
                    {
                        const int iBaseVertex = static_cast<int>(m_MyMeshParts[i].m_Vertices.size());
                        const auto iBaseIndex = m_MyMeshParts[i].m_Indices.size();
                        m_MyMeshParts[i].m_Vertices.insert(m_MyMeshParts[i].m_Vertices.end(), m_MyMeshParts[j].m_Vertices.begin(), m_MyMeshParts[j].m_Vertices.end());
                        m_MyMeshParts[i].m_Indices.insert(m_MyMeshParts[i].m_Indices.end(), m_MyMeshParts[j].m_Indices.begin(), m_MyMeshParts[j].m_Indices.end());
                        // Fix the indices
                        for (auto I = iBaseIndex; I < m_MyMeshParts[i].m_Indices.size(); ++I)
                        {
                            m_MyMeshParts[i].m_Indices[I] += iBaseVertex;
                        }
                        m_MyMeshParts.erase(m_MyMeshParts.begin() + j);
                        --j;
                    }
                }
            }

            //
            // Create final structure
            //
            std::unordered_map<std::string, int> MeshNameToIndex;
            for (auto& E : m_MyMeshParts)
            {
                int iFinalMesh = -1;
                auto it = MeshNameToIndex.find(E.m_MeshName);
                if (it != MeshNameToIndex.end())
                {
                    iFinalMesh = it->second;
                }
                else
                {
                    iFinalMesh = static_cast<int>(m_pGeom->m_Mesh.size());
                    m_pGeom->m_Mesh.emplace_back();
                    m_pGeom->m_Mesh.back().m_ScenePath  = E.m_FullMeshName;
                    m_pGeom->m_Mesh.back().m_Name       = E.m_MeshName.empty() ? "Default" : E.m_MeshName;
                    m_pGeom->m_Mesh.back().m_nBones     = 0; // To be computed later if needed
                    MeshNameToIndex[E.m_MeshName]       = iFinalMesh;
                }

                const int baseVertex = static_cast<int>(m_pGeom->m_Vertex.size());
                m_pGeom->m_Vertex.insert(m_pGeom->m_Vertex.end(), E.m_Vertices.begin(), E.m_Vertices.end());
                for (auto k = 0u; k < E.m_Indices.size(); k += 3)
                {
                    auto& f = m_pGeom->m_Facet.emplace_back();
                    f.m_iMesh       = iFinalMesh;
                    f.m_nVertices   = 3;
                    f.m_iVertex[0]  = E.m_Indices[k + 0] + baseVertex;
                    f.m_iVertex[1]  = E.m_Indices[k + 1] + baseVertex;
                    f.m_iVertex[2]  = E.m_Indices[k + 2] + baseVertex;
                    f.m_iMaterialInstance = E.m_iMaterialInstance; // Will be remapped in ImportMaterials

                    // Compute plane
                    const auto&     p0      = m_pGeom->m_Vertex[f.m_iVertex[0]].m_Position;
                    const auto&     p1      = m_pGeom->m_Vertex[f.m_iVertex[1]].m_Position;
                    const auto&     p2      = m_pGeom->m_Vertex[f.m_iVertex[2]].m_Position;
                    xmath::fvec3    normal  = (p1 - p0).Cross(p2 - p0).NormalizeSafe();
                    const float     d       = -normal.Dot(p0);
                    f.m_Plane = xmath::fplane(normal.m_X, normal.m_Y, normal.m_Z, d);
                }
            }
        }

        //------------------------------------------------------------------------------------------------------

        void ImportAnimations( int MaxSamplingFPS = 60 ) noexcept
        {
            struct indices
            {
                std::uint32_t m_iPositions{ 0 };
                std::uint32_t m_iRotations{ 0 };
                std::uint32_t m_iScales { 0 };
            };

            m_pAnims->resize(m_pScene->mNumAnimations);
            for( auto i = 0ul; i < m_pScene->mNumAnimations; ++i )
            {
                const aiAnimation&  AssimpAnim          = *m_pScene->mAnimations[i];
                const int           SamplingFPS         = static_cast<int>(MaxSamplingFPS > AssimpAnim.mTicksPerSecond ? AssimpAnim.mTicksPerSecond : MaxSamplingFPS);
                const double        AnimationDuration   = AssimpAnim.mDuration / AssimpAnim.mTicksPerSecond;
                const double        DeltaTime           = (AssimpAnim.mTicksPerSecond / SamplingFPS);
                const int           FrameCount          = (int)std::ceil( AssimpAnim.mDuration / DeltaTime);
                std::vector<indices> LastPositions;

                assert(FrameCount > 0);

                // Allocate all the bones for this animation
                // assert( AssimpAnim.mNumChannels <= m_pSkeleton->m_Bones.size() );
                auto& MyAnim = (*m_pAnims)[i];
                MyAnim.m_Bone.resize(m_InternalBones.size());
                MyAnim.m_KeyFrame.resize(FrameCount * MyAnim.m_Bone.size());
                MyAnim.m_FPS        = SamplingFPS;
                MyAnim.m_Name       = AssimpAnim.mName.C_Str();
                MyAnim.m_nFrames    = FrameCount;

                // To cache the last positions for a given frame for each bone
                LastPositions.resize( AssimpAnim.mNumChannels );

                // Create/Sample all the frames
                for( int iFrame = 0; iFrame < FrameCount; iFrame++ )
                {
                    const auto t = iFrame * DeltaTime;
                    for( auto b = 0ul; b < AssimpAnim.mNumChannels; ++b )
                    {
                        const aiNodeAnim&   Channel = *AssimpAnim.mChannels[b];
                        auto&               LastPos = LastPositions[b];

                        // Sample the position key
                        aiVector3D presentPosition(0, 0, 0);
                        if( Channel.mNumPositionKeys > 0 )
                        {
                            // Update the Position Index for the given bone
                            while( LastPos.m_iPositions < Channel.mNumPositionKeys - 1 )
                            {
                                if( t < Channel.mPositionKeys[LastPos.m_iPositions + 1].mTime ) break;
                                ++LastPos.m_iPositions;
                            }
                            // interpolate between this frame's value and next frame's value
                            unsigned int        NextFrame   = (LastPos.m_iPositions + 1) % Channel.mNumPositionKeys;
                            const aiVectorKey&  Key         = Channel.mPositionKeys[ LastPos.m_iPositions ];
                            const aiVectorKey&  NextKey     = Channel.mPositionKeys[ NextFrame ];
                            double              diffTime    = NextKey.mTime - Key.mTime;
                            if ( diffTime < 0.0 ) diffTime += AssimpAnim.mDuration;
                            if ( diffTime > 0 )
                            {
                                float factor = float((t - Key.mTime) / diffTime);
                                presentPosition = Key.mValue + (NextKey.mValue - Key.mValue) * factor;
                            }
                            else
                            {
                                presentPosition = Key.mValue;
                            }
                        }

                        // Sample the Rotation key
                        aiQuaternion presentRotation(1, 0, 0, 0);
                        if( Channel.mNumRotationKeys > 0 )
                        {
                            // Update the Rotation Index for the given bone
                            while (LastPos.m_iRotations < Channel.mNumRotationKeys - 1)
                            {
                                if (t < Channel.mRotationKeys[LastPos.m_iRotations + 1].mTime) break;
                                ++LastPos.m_iRotations;
                            }

                            // interpolate between this frame's value and next frame's value
                            unsigned int        NextFrame   = (LastPos.m_iRotations + 1) % Channel.mNumRotationKeys;
                            const aiQuatKey&    Key         = Channel.mRotationKeys[LastPos.m_iRotations];
                            const aiQuatKey&    NextKey     = Channel.mRotationKeys[NextFrame];
                            double              diffTime    = NextKey.mTime - Key.mTime;
                            if( diffTime < 0.0 ) diffTime += AssimpAnim.mDuration;
                            if( diffTime > 0 )
                            {
                                float factor = float((t - Key.mTime) / diffTime);
                                aiQuaternion::Interpolate(presentRotation, Key.mValue, NextKey.mValue, factor);
                            }
                            else
                            {
                                presentRotation = Key.mValue;
                            }
                        }

                        // Sample the Scale key
                        aiVector3D presentScaling( 1, 1, 1 );
                        if( Channel.mNumScalingKeys > 0 )
                        {
                            // Update the Rotation Index for the given bone
                            while( LastPos.m_iScales < Channel.mNumScalingKeys - 1)
                            {
                                if (t < Channel.mScalingKeys[LastPos.m_iScales + 1].mTime) break;
                                ++LastPos.m_iScales;
                            }

                            // interpolate between this frame's value and next frame's value
                            unsigned int        NextFrame   = (LastPos.m_iScales + 1) % Channel.mNumScalingKeys;
                            const aiVectorKey&  Key         = Channel.mScalingKeys[ LastPos.m_iScales];
                            const aiVectorKey&  NextKey     = Channel.mScalingKeys[ NextFrame ];
                            double              diffTime    = NextKey.mTime - Key.mTime;
                            if ( diffTime < 0.0 ) diffTime += AssimpAnim.mDuration;
                            if ( diffTime > 0 )
                            {
                                float factor = float((t - Key.mTime) / diffTime);
                                presentScaling = Key.mValue + (NextKey.mValue - Key.mValue) * factor;
                            }
                            else
                            {
                                presentScaling = Key.mValue;
                            }
                        }

                        //
                        // Set all the computer components into our frame
                        //

                        // make sure that we can find the bone
                        const int iBone = m_pGeom->getBoneIDFromName(Channel.mNodeName.C_Str());
                        if (-1 == iBone)
                        {
                            continue;
                        }

                        auto& MyBoneKeyFrame = MyAnim.m_KeyFrame[iFrame * MyAnim.m_Bone.size() + iBone];
                        MyBoneKeyFrame.m_Position = xmath::fvec3(presentPosition.x, presentPosition.y, presentPosition.z);
                        MyBoneKeyFrame.m_Rotation = xmath::fquat(presentRotation.x, presentRotation.y, presentRotation.z, presentRotation.w).NormalizeSafe();
                        MyBoneKeyFrame.m_Scale = xmath::fvec3(presentScaling.x, presentScaling.y, presentScaling.z);
                    }
                }

                //
                // Add transforms without animations
                //
                for (int i = 0; i < m_InternalBones.size(); ++i)
                {
                    auto pNode = m_pScene->mRootNode->FindNode(m_InternalBones[i].name.c_str());

                    aiQuaternion Q;
                    aiVector3D S;
                    aiVector3D T;
                    pNode->mTransformation.Decompose(S, Q, T);
                    bool hasKeys = false;
                    for (int f = 0; f < FrameCount; ++f)
                    {
                        if (MyAnim.m_KeyFrame[f * MyAnim.m_Bone.size() + i].m_Scale.m_X != 0) { hasKeys = true; break; }
                    }

                    if (hasKeys) continue;

                    for (int f = 0; f < FrameCount; ++f)
                    {
                        auto& MyBoneKeyFrame = MyAnim.m_KeyFrame[f * MyAnim.m_Bone.size() + i];
                        MyBoneKeyFrame.m_Position = xmath::fvec3(T.x, T.y, T.z);
                        MyBoneKeyFrame.m_Rotation = xmath::fquat(Q.x, Q.y, Q.z, Q.w).NormalizeSafe();
                        MyBoneKeyFrame.m_Scale    = xmath::fvec3(S.x, S.y, S.z);
                    }
                }

                // Copy bone info
                for (int b = 0; b < MyAnim.m_Bone.size(); ++b)
                {
                    auto&       abone = MyAnim.m_Bone[b];

                    abone.m_Name                = m_InternalBones[b].name;
                    abone.m_iParent             = m_InternalBones[b].parent;
                    abone.m_nChildren           = 0;
                    abone.m_BindTranslation     = m_InternalBones[b].pos;
                    abone.m_BindRotation        = m_InternalBones[b].rot;
                    abone.m_BindScale           = m_InternalBones[b].scale;
                    abone.m_bScaleKeys          = true;
                    abone.m_bRotationKeys       = true;
                    abone.m_bTranslationKeys    = true;
                    abone.m_bIsMasked           = false;
                    abone.m_BindMatrix          = xmath::fmat4(abone.m_BindScale, abone.m_BindRotation, abone.m_BindTranslation);
                    abone.m_BindMatrixInv       = abone.m_BindMatrix.Inverse();
                    abone.m_NeutralPose         = m_InternalBones[b].neutral;
                }

                for (auto& bone : MyAnim.m_Bone)
                {
                    if (bone.m_iParent >= 0) MyAnim.m_Bone[bone.m_iParent].m_nChildren++;
                }
            }
        }

        //------------------------------------------------------------------------------------------------------

        aiMatrix4x4 GetGlobalTransform(const aiNode* pNode) const noexcept
        {
            aiMatrix4x4 mat = pNode->mTransformation;
            for (pNode = pNode->mParent; pNode; pNode = pNode->mParent)
            {
                mat = pNode->mTransformation * mat;
            }
            return mat;
        }

        //------------------------------------------------------------------------------------------------------

        bool MatricesEqual(const aiMatrix4x4& a, const aiMatrix4x4& b, float epsilon = 1e-5f) noexcept
        {
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    if (std::abs(a[i][j] - b[i][j]) > epsilon) return false;
            return true;
        }

        //------------------------------------------------------------------------------------------------------

        void ImportSkeleton() noexcept
        {
            std::unordered_map<std::string, const aiNode*>  NameToNode;
            std::unordered_map<std::string, const aiBone*>  NameToBone;
            std::unordered_map<std::string, aiMatrix4x4>    NameToMeshGlobal;
            std::unordered_map<std::string, const aiNode*>  NameToMeshNode; // For reference if needed
            //
            // Add bones based on bone associated by meshes
            //
            for (auto iMesh = 0u; iMesh < m_pScene->mNumMeshes; ++iMesh)
            {
                const aiMesh&       Mesh        = *m_pScene->mMeshes[iMesh];
                const aiNode*       pMeshNode   = m_MeshReferences[iMesh].m_Nodes[0];
                const aiMatrix4x4   MeshGlobal  = GetGlobalTransform(pMeshNode);

                if (!Mesh.HasBones()) continue;

                for (auto iBone = 0u; iBone < Mesh.mNumBones; ++iBone)
                {
                    const aiBone&       Bone = *Mesh.mBones[iBone];
                    const std::string   Name = Bone.mName.C_Str();

                    if (NameToBone.count(Name))
                    {
                        // Check consistency
                        if (!MatricesEqual(NameToBone[Name]->mOffsetMatrix, Bone.mOffsetMatrix))
                        {
                            printf("WARNING: Bone %s has different offset matrices in different meshes\n", Name.c_str());
                        }

                        if (!MatricesEqual(NameToMeshGlobal[Name], MeshGlobal))
                        {
                            printf("WARNING: Bone %s has different mesh global transforms in different meshes\n", Name.c_str());
                        }
                    }
                    else
                    {
                        NameToBone[Name]        = &Bone;
                        NameToMeshGlobal[Name]  = MeshGlobal;
                        NameToMeshNode[Name]    = pMeshNode;
                        NameToNode[Name]        = m_pScene->mRootNode->FindNode(Bone.mName);
                    }
                }
            }

            //
            // Make sure all the parent nodes are inserted in the hash table
            //
            for (auto itr1 : NameToNode)
            {
                for (auto pParentNode = NameToNode.find(itr1.first)->second->mParent; pParentNode != nullptr; pParentNode = pParentNode->mParent)
                {
                    if (auto e = NameToNode.find(pParentNode->mName.C_Str()); e == NameToNode.end())
                    {
                        NameToNode[pParentNode->mName.C_Str()] = pParentNode;
                    }
                }
            }

            //
            // Check to see if we readed too many bones!
            //
            if (NameToNode.size() > 0xff)
            {
                printf("ERROR: This mesh has %zd Bones we can only handle up to 256\n", NameToNode.size());
            }

            //
            // Organize build the skeleton
            // We want the parents to be first then the children
            // Ideally we also want to have the bones that have more children higher
            //
            struct proto
            {
                const aiNode*   m_pAssimpNode       { nullptr };
                int             m_Depth             { 0 };
                int             m_nTotalChildren    { 0 };
                int             m_nChildren         { 0 };
            };

            std::vector<proto> Proto;

            // Set the Assimp Node
            Proto.resize(NameToNode.size());
            {
                int i = 0;
                for (auto itr = NameToNode.begin(); itr != NameToNode.end(); ++itr)
                {
                    auto& P = Proto[i++];
                    P.m_pAssimpNode = itr->second;
                }
            }

            // Set the Depth, m_nTotalChildren and nChildren
            for (auto i = 0u; i < Proto.size(); ++i)
            {
                auto&   P               = Proto[i];
                bool    bFoundParent    = false;

                for (aiNode* pNode = P.m_pAssimpNode->mParent; pNode; pNode = pNode->mParent)
                {
                    P.m_Depth++;

                    // If we can find the parent lets keep a count of how many total children it has
                    for (auto j = 0; j < Proto.size(); ++j)
                    {
                        auto& ParentProto = Proto[j];
                        if (pNode == ParentProto.m_pAssimpNode)
                        {
                            ParentProto.m_nTotalChildren++;
                            if (bFoundParent == false) ParentProto.m_nChildren++;
                            bFoundParent = true;
                            break;
                        }
                    }
                }
            }

            // Put all the Proto bones in the right order
            // Make it deterministic
            std::qsort(Proto.data(), Proto.size(), sizeof(proto), [](const void* pA, const void* pB) -> int
                {
                    const auto& A = *reinterpret_cast<const proto*>(pA);
                    const auto& B = *reinterpret_cast<const proto*>(pB);
                    if (A.m_Depth < B.m_Depth) return -1;
                    if (A.m_Depth > B.m_Depth) return 1;
                    if (A.m_nTotalChildren < B.m_nTotalChildren) return -1;
                    if (A.m_nTotalChildren > B.m_nTotalChildren) return 1;
                    return std::strcmp(A.m_pAssimpNode->mName.C_Str(), B.m_pAssimpNode->mName.C_Str());
                });

            //
            // Create all the real bones
            //
            m_InternalBones.resize(Proto.size());
            {
                int i = 0;

                for (auto& internalBone : m_InternalBones)
                {
                    auto& ProtoBone = Proto[i++];
                    internalBone.name   = ProtoBone.m_pAssimpNode->mName.data;
                    internalBone.parent = -1;

                    // Potentially we may not have all parent nodes in our skeleton
                    // so we must search by each of the potential assimp nodes
                    for (aiNode* pNode = ProtoBone.m_pAssimpNode->mParent; internalBone.parent == -1 && pNode; pNode = pNode->mParent)
                    {
                        for (auto j = 0; j < i; ++j)
                        {
                            if (Proto[j].m_pAssimpNode == ProtoBone.m_pAssimpNode->mParent)
                            {
                                internalBone.parent = j;
                                break;
                            }
                        }
                    }

                    // Decompose local transform
                    aiVector3D      S, T;
                    aiQuaternion    Q;

                    ProtoBone.m_pAssimpNode->mTransformation.Decompose(S, Q, T);
                    internalBone.scale  = xmath::fvec3(S.x, S.y, S.z);
                    internalBone.rot.setup(Q.x, Q.y, Q.z, Q.w).NormalizeSafe();
                    internalBone.pos    = xmath::fvec3(T.x, T.y, T.z);

                    // Check if we have a binding matrix
                    if (auto B = NameToBone.find(ProtoBone.m_pAssimpNode->mName.data); B != NameToBone.end())
                    {
                        auto OffsetMatrix   = B->second->mOffsetMatrix;
                        auto NodeMatrix     = NameToMeshGlobal[ProtoBone.m_pAssimpNode->mName.data];
                        auto Adjusted       = OffsetMatrix * NodeMatrix.Inverse();

                        aiMatrix4x4t<float> adjustedT = Adjusted.Transpose();
                        internalBone.invBind = xmath::fmat4();
                        std::memcpy(&internalBone.invBind, &adjustedT, sizeof(xmath::fmat4));
                    }
                    else
                    {
                        internalBone.invBind = xmath::fmat4::fromIdentity();
                    }

                    // Neutral pose
                    {
                        const auto  C           = NameToNode.find(ProtoBone.m_pAssimpNode->mName.data);
                        auto        NodeMatrix  = C->second->mTransformation;
                        for (auto p = C->second->mParent; p; p = p->mParent) NodeMatrix = p->mTransformation * NodeMatrix;

                        aiMatrix4x4t<float> nodeT = NodeMatrix.Transpose();
                        internalBone.neutral = xmath::fmat4();
                        std::memcpy(&internalBone.neutral, &nodeT, sizeof(xmath::fmat4));
                        internalBone.neutral *= internalBone.invBind;
                    }
                }
            }

            // Normalization: Find root and apply root bind to all invBinds
            int rootIndex = -1;
            for (int idx = 0; idx < m_InternalBones.size(); ++idx)
            {
                if (m_InternalBones[idx].parent == -1)
                {
                    rootIndex = idx;
                    break;
                }
            }

            if (rootIndex != -1)
            {
                auto        rootNode = NameToNode[m_InternalBones[rootIndex].name];
                const auto  rootBind = GetGlobalTransform(rootNode);

                // Apply to all bones' invBind
                for (auto& internalBone : m_InternalBones)
                {
                    aiMatrix4x4 origInvBindAi;
                    std::memcpy(&origInvBindAi, &internalBone.invBind, sizeof(aiMatrix4x4));

                    origInvBindAi = origInvBindAi.Transpose(); // Back to row-major

                    aiMatrix4x4 newInvBindAi = origInvBindAi * rootBind;
                    aiMatrix4x4t<float> newT = newInvBindAi.Transpose();
                    std::memcpy(&internalBone.invBind, &newT, sizeof(xmath::fmat4));
                }
            }

            // Compute nChildren for internal
            std::vector<int> nChildren(m_InternalBones.size(), 0);
            for (auto& internalBone : m_InternalBones)
            {
                if (internalBone.parent >= 0) nChildren[internalBone.parent]++;
            }

            for (int idx = 0; idx < m_InternalBones.size(); ++idx)
            {
                m_InternalBones[idx].nChildren = nChildren[idx]; // Add nChildren to internal_bone struct
            }

            // Wait, add int nChildren to internal_bone
            // Fill geom if requested
            if (m_pGeom)
            {
                m_pGeom->m_Bone.resize(m_InternalBones.size());
                for (int idx = 0; idx < m_InternalBones.size(); ++idx)
                {
                    auto& gbone = m_pGeom->m_Bone[idx];
                    gbone.m_Name        = m_InternalBones[idx].name;
                    gbone.m_nChildren   = m_InternalBones[idx].nChildren;
                    gbone.m_iParent     = m_InternalBones[idx].parent;
                    gbone.m_Scale       = m_InternalBones[idx].scale;
                    gbone.m_Rotation    = m_InternalBones[idx].rot;
                    gbone.m_Position    = m_InternalBones[idx].pos;
                    gbone.m_BBox        = xmath::fbbox(); // Empty
                }
            }

            // Fill pSkeleton if requested
            if (m_pSkeleton)
            {
                m_pSkeleton->m_nFrames  = 1;
                m_pSkeleton->m_FPS      = 0;
                m_pSkeleton->m_Name     = "BindPose";
                m_pSkeleton->m_Bone.resize(m_InternalBones.size());
                m_pSkeleton->m_KeyFrame.resize(m_InternalBones.size());

                for (int idx = 0; idx < m_InternalBones.size(); ++idx)
                {
                    auto& abone = m_pSkeleton->m_Bone[idx];
                    abone.m_Name                = m_InternalBones[idx].name;
                    abone.m_iParent             = m_InternalBones[idx].parent;
                    abone.m_nChildren           = m_InternalBones[idx].nChildren;
                    abone.m_BindTranslation     = m_InternalBones[idx].pos;
                    abone.m_BindRotation        = m_InternalBones[idx].rot;
                    abone.m_BindScale           = m_InternalBones[idx].scale;
                    abone.m_bScaleKeys          = false;
                    abone.m_bRotationKeys       = false;
                    abone.m_bTranslationKeys    = false;
                    abone.m_bIsMasked           = false;
                    abone.m_BindMatrix          = xmath::fmat4(abone.m_BindScale, abone.m_BindRotation, abone.m_BindTranslation);
                    abone.m_BindMatrixInv       = m_InternalBones[idx].invBind;
                    abone.m_NeutralPose         = m_InternalBones[idx].neutral;

                    auto& kf        = m_pSkeleton->m_KeyFrame[idx];
                    kf.m_Scale      = abone.m_BindScale;
                    kf.m_Rotation   = abone.m_BindRotation;
                    kf.m_Position   = abone.m_BindTranslation;
                }
            }
        }

        //------------------------------------------------------------------------------------------------------

        void ImportGeometrySkin(std::vector<myMeshPart>& MyNodes) noexcept
        {
            // Find root index from skeleton
            int rootIndex = -1;
            for (int idx = 0; idx < m_pGeom->m_Bone.size(); ++idx)
            {
                if (m_pGeom->m_Bone[idx].m_iParent == -1)
                {
                    rootIndex = idx;
                    break;
                }
            }

            aiMatrix4x4 rootBind;
            aiMatrix4x4 invRootBind;
            if (rootIndex != -1)
            {
                auto rootNode = m_pScene->mRootNode->FindNode(m_pGeom->m_Bone[rootIndex].m_Name.c_str());
                rootBind    = GetGlobalTransform(rootNode);
                invRootBind = rootBind.Inverse();
            }
            else
            {
                rootBind    = aiMatrix4x4();
                invRootBind = aiMatrix4x4();
            }

            //
            // Add bones based on bone associated by meshes
            //
            MyNodes.resize(m_pScene->mNumMeshes);
            for (auto iMesh = 0u; iMesh < m_pScene->mNumMeshes; ++iMesh)
            {
                const aiMesh&   AssimpMesh      = *m_pScene->mMeshes[iMesh];
                int             iTexCordinates;

                if (ImportGeometryValidateMesh(AssimpMesh, iTexCordinates)) continue;

                //
                // Copy mesh name and Material Index
                //
                MyNodes[iMesh].m_MeshName           = AssimpMesh.mName.C_Str();
                MyNodes[iMesh].m_iMaterialInstance  = AssimpMesh.mMaterialIndex;

                // Get the global transform for the mesh node
                const aiNode*       pMeshNode   = m_MeshReferences[iMesh].m_Nodes[0];
                const aiMatrix4x4   MeshGlobal  = GetGlobalTransform(pMeshNode);

                // Combined transform for normalization: invRootBind * MeshGlobal
                aiMatrix4x4 combinedTransform = invRootBind * MeshGlobal;

                // Decompose combined for rotation (ignore scale for normals/tangents)
                aiQuaternion combinedRotation;
                aiVector3D combinedScale, combinedPosition;
                combinedTransform.Decompose(combinedScale, combinedRotation, combinedPosition);

                //
                // Copy Vertices
                //
                MyNodes[iMesh].m_Vertices.resize(AssimpMesh.mNumVertices);
                for (auto i = 0u; i < AssimpMesh.mNumVertices; ++i)
                {
                    xraw3d::geom::vertex&   Vertex  = MyNodes[iMesh].m_Vertices[i];
                    aiVector3D              pos     = AssimpMesh.mVertices[i];
                    pos = combinedTransform * pos; // To world, then normalize

                    Vertex.m_Position.setup(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));

                    // Collect multiple UVs
                    Vertex.m_nUVs = 0;
                    const auto numUVChannels = static_cast<int>(AssimpMesh.GetNumUVChannels());
                    for (int ch = 0; ch < numUVChannels && Vertex.m_nUVs < geom::vertex_max_uv_v; ++ch)
                    {
                        if (AssimpMesh.HasTextureCoords(ch))
                        {
                            aiVector3D uv = AssimpMesh.mTextureCoords[ch][i];
                            Vertex.m_UV[Vertex.m_nUVs++] = xmath::fvec2(uv.x, uv.y);
                        }
                    }

                    // Collect multiple colors
                    Vertex.m_nColors = 0;
                    const auto numColorChannels = static_cast<int>(AssimpMesh.GetNumColorChannels());
                    for (int ch = 0; ch < numColorChannels && Vertex.m_nColors < geom::vertex_max_colors_v; ++ch)
                    {
                        if (AssimpMesh.HasVertexColors(ch))
                        {
                            aiColor4D c = AssimpMesh.mColors[ch][i];
                            Vertex.m_Color[Vertex.m_nColors++].setupFromRGBA(c.r, c.g, c.b, c.a);
                        }
                    }

                    if (AssimpMesh.HasTangentsAndBitangents())
                    {
                        assert(AssimpMesh.HasNormals());
                        const auto T = combinedRotation.Rotate(AssimpMesh.mTangents[i]);
                        const auto B = combinedRotation.Rotate(AssimpMesh.mBitangents[i]);
                        const auto N = combinedRotation.Rotate(AssimpMesh.mNormals[i]);
                        Vertex.m_nNormals = 1;
                        Vertex.m_nTangents = 1;
                        Vertex.m_nBinormals = 1;
                        Vertex.m_BTN[0].m_Normal.setup(N.x, N.y, N.z);
                        Vertex.m_BTN[0].m_Tangent.setup(T.x, T.y, T.z);
                        Vertex.m_BTN[0].m_Binormal.setup(B.x, B.y, B.z);
                        Vertex.m_BTN[0].m_Normal.NormalizeSafe();
                        Vertex.m_BTN[0].m_Tangent.NormalizeSafe();
                        Vertex.m_BTN[0].m_Binormal.NormalizeSafe();
                    }
                    else
                    {
                        const auto N = combinedRotation.Rotate(AssimpMesh.mNormals[i]);
                        Vertex.m_nNormals = 1;
                        Vertex.m_nTangents = 1;
                        Vertex.m_nBinormals = 1;
                        Vertex.m_BTN[0].m_Normal.setup(N.x, N.y, N.z);
                        Vertex.m_BTN[0].m_Tangent.setup(1, 0, 0);
                        Vertex.m_BTN[0].m_Binormal.setup(0, 1, 0);
                        Vertex.m_BTN[0].m_Normal.NormalizeSafe();
                    }

                    Vertex.m_nWeights = 0;
                    Vertex.m_iFrame   = 0;
                }

                //
                // Copy the indices
                //
                for (auto i = 0u; i < AssimpMesh.mNumFaces; ++i)
                {
                    const auto& Face = AssimpMesh.mFaces[i];
                    for (auto j = 0u; j < Face.mNumIndices; ++j)
                        MyNodes[iMesh].m_Indices.push_back(Face.mIndices[j]);
                }

                //
                // Add the bone weights
                //
                if (AssimpMesh.mNumBones > 0)
                {
                    struct tmp_weight
                    {
                        std::int32_t    m_iBone;
                        float           m_Weight{ 0 };
                    };

                    struct my_weights
                    {
                        int                                                 m_Count{ 0 };
                        std::array<tmp_weight, geom::vertex_max_weights_v>  m_Weights;
                    };

                    std::vector<my_weights> MyWeights;
                    MyWeights.resize(AssimpMesh.mNumVertices);
                    //
                    // Collect bones indices and weights
                    //
                    assert(m_MeshReferences[iMesh].m_Nodes.size() == 1);
                    MyNodes[iMesh].m_MeshName = GetMeshNameFromNode(*m_MeshReferences[iMesh].m_Nodes[0]);
                    for (auto iBone = 0u; iBone < AssimpMesh.mNumBones; iBone++)
                    {
                        const auto& AssimpBone  = *AssimpMesh.mBones[iBone];
                        int         boneIndex   = m_pGeom->getBoneIDFromName(AssimpBone.mName.C_Str());
                        if (boneIndex < 0 || boneIndex >= m_pGeom->m_Bone.size())
                        {
                            printf("WARNING: Invalid bone '%s' (index %d) in mesh %d; clamping to 0\n", AssimpBone.mName.C_Str(), boneIndex, iMesh);
                            boneIndex = 0; // Or skip: continue;
                        }

                        const std::int32_t iSkeletonBone = static_cast<std::int32_t>(boneIndex);
                        for (auto iWeight = 0u; iWeight < AssimpBone.mNumWeights; ++iWeight)
                        {
                            const auto& AssimpWeight    = AssimpBone.mWeights[iWeight];
                            auto&       MyWeight        = MyWeights[AssimpWeight.mVertexId];

                            MyWeight.m_Weights[MyWeight.m_Count].m_iBone = iSkeletonBone;
                            MyWeight.m_Weights[MyWeight.m_Count].m_Weight = AssimpWeight.mWeight;

                            // get ready for the next one
                            MyWeight.m_Count++;
                        }
                    }

                    //
                    // Sort weights, normalize and set to the final vert
                    //
                    for (int iVertex = 0u; iVertex < MyWeights.size(); ++iVertex)
                    {
                        auto& E = MyWeights[iVertex];

                        // Sort from bigger to smaller
                        std::sort(E.m_Weights.begin(), E.m_Weights.begin() + E.m_Count, [](const tmp_weight& a, const tmp_weight& b)
                            {
                                return a.m_Weight > b.m_Weight;
                            });

                        // Normalize the weights
                        float Total = 0;
                        for (int i = 0; i < E.m_Count; ++i)
                        {
                            Total += E.m_Weights[i].m_Weight;
                        }

                        if (Total > 0)
                        {
                            for (int i = 0; i < E.m_Count; ++i)
                            {
                                E.m_Weights[i].m_Weight /= Total;
                            }
                        }

                        // Copy Weight To the Vert
                        auto& V = MyNodes[iMesh].m_Vertices[iVertex];
                        V.m_nWeights = E.m_Count;
                        for (int i = 0; i < E.m_Count && i < geom::vertex_max_weights_v; ++i)
                        {
                            const auto& BW = E.m_Weights[i];
                            V.m_Weight[i].m_iBone = BW.m_iBone;
                            V.m_Weight[i].m_Weight = BW.m_Weight;
                        }
                    }

                    //
                    // Sanity check (make sure that all the vertices have bone and weights
                    //
                    for (auto& V : MyNodes[iMesh].m_Vertices)
                    {
                        assert(V.m_nWeights > 0);
                    }
                }
                else
                {
                    //
                    // Set the weights and duplicate mesh if needed
                    //

                    // Remember where was the base
                    int iBase = static_cast<int>(MyNodes.size());

                    // Grow the total number of meshes if we have to...
                    if (m_MeshReferences[iMesh].m_Nodes.size() > 1) MyNodes.resize(MyNodes.size() + m_MeshReferences[iMesh].m_Nodes.size() - 1);

                    auto pMyNode = &MyNodes[iMesh];
                    for (auto k = 0u; k < m_MeshReferences[iMesh].m_Nodes.size(); ++k)
                    {
                        const auto  pN          = m_MeshReferences[iMesh].m_Nodes[k];
                        int         boneIndex   = m_pGeom->getBoneIDFromName(pN->mName.C_Str());

                        pMyNode->m_MeshName = GetMeshNameFromNode(*pN);

                        if (boneIndex < 0 || boneIndex >= static_cast<int>(m_pGeom->m_Bone.size()))
                        {
                            printf("WARNING: Invalid bone for node '%s' (index %d) in mesh %d; using root (0)\n", pN->mName.C_Str(), boneIndex, iMesh);
                            boneIndex = 0; // Fallback to root; or continue to skip
                        }

                        const std::int32_t iSkeletonBone = static_cast<std::int32_t>(boneIndex);
                        for (auto iVertex = 0u; iVertex < AssimpMesh.mNumVertices; ++iVertex)
                        {
                            auto& V = pMyNode->m_Vertices[iVertex];
                            V.m_nWeights = 1;
                            V.m_Weight[0].m_iBone   = iSkeletonBone;
                            V.m_Weight[0].m_Weight  = 1.0f;
                        }

                        if (k + 1 < m_MeshReferences[iMesh].m_Nodes.size())
                        {
                            pMyNode     = &MyNodes[iBase++];
                            *pMyNode    = MyNodes[iMesh]; // Deep copy
                        }
                    }
                }
            }
        }

    protected:

        std::vector<internal_bone>  m_InternalBones;
        std::vector<refs>           m_MeshReferences;
        std::vector<myMeshPart>     m_MyMeshParts;
        std::vector<xraw3d::anim>*  m_pAnims        = nullptr;
        xraw3d::geom*               m_pGeom         = nullptr;
        xraw3d::anim*               m_pSkeleton     = nullptr;
        const aiScene*              m_pScene        = nullptr;
        bool                        m_bStaticGeom;
    };
}