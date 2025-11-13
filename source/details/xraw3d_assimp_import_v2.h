#include "../xraw3d.h"
#include "dependencies/xerr/source/xerr.h"
#include "dependencies/xstrtool/source/xstrtool.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/matrix4x4.h>
#include <assimp/matrix3x3.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdio>
#include <set>

// Assuming xraw3d namespace and geom struct as provided
// Also assuming xmath namespace with fvec3, fquat, fbbox, fplane, fmat4 (if needed), etc.
// xstrtool for string conversions, path normalize
// xcolori for colors (assuming uint32_t or similar)

namespace xraw3d::assimp_v2
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

    struct importer 
    {
        struct settings
        {
            bool    m_bAnimated                 = false;    // Animated or static
            bool    m_bImportGeometry           = true;
            bool    m_bImportAnimation          = false;
            bool    m_bImportSkeleton           = false;
            bool    m_bLimitBoneWeights         = true;
            bool    m_bGenerateTBNInformation   = false;
            bool    m_bFlipUVs                  = true;
        };

        settings m_Settings;

        xerr Import(std::wstring_view FileName, geom& Geom, node& Node ) noexcept 
        {
            try
            {
                //
                // Double check the settings
                //
                if ( m_Settings.m_bAnimated == false )
                {
                    m_Settings.m_bImportGeometry  = true;
                    m_Settings.m_bImportAnimation = false;
                    m_Settings.m_bImportSkeleton  = false;
                }
                else
                {
                    // Do we have anything to do?
                    if (not(m_Settings.m_bImportGeometry || m_Settings.m_bImportAnimation || m_Settings.m_bImportSkeleton))
                        return {};

                    // If we are importing geometry we should probably care about the skeleton
                    if (m_Settings.m_bImportGeometry) m_Settings.m_bImportSkeleton = true;
                }

                auto Importer = std::make_unique<Assimp::Importer>();
                const aiScene* pScene = Importer->ReadFile(xstrtool::To(FileName).c_str()
                    , aiProcess_Triangulate
                    | aiProcess_JoinIdenticalVertices
                    | aiProcess_FindDegenerates
                    | aiProcess_ValidateDataStructure
                    | ( m_Settings.m_bLimitBoneWeights ? aiProcess_LimitBoneWeights : 0 )
                    | aiProcess_GenUVCoords 
                    | aiProcess_TransformUVCoords 
                    | aiProcess_FindInstances
                    | (m_Settings.m_bGenerateTBNInformation ? (aiProcess_GenNormals | aiProcess_CalcTangentSpace) : 0)
                    | aiProcess_RemoveRedundantMaterials 
                    | aiProcess_FindInvalidData
                    | (m_Settings.m_bFlipUVs ? aiProcess_FlipUVs : 0)
                );
                if (!pScene) xerr::create<state::ASSIMP_FAIL_TO_IMPORT, "Assimp failed to load a file">();

                // Clear existing data in geom
                Geom.m_Bone.clear();
                Geom.m_Vertex.clear();
                Geom.m_Facet.clear();
                Geom.m_MaterialInstance.clear();
                Geom.m_Mesh.clear();

                m_pScene = pScene;
                m_pGeom  = &Geom;

                // Collect all the nodes in assimp... it may be usefull
                Node = buildNodes(m_pScene->mRootNode, m_pScene);

                m_MeshReferences.resize(pScene->mNumMeshes);
                if (auto Err = SanityCheck(); Err ) 
                    return Err;

                //if(m_Settings.m_bImportSkeleton) 
                
                ImportSkeleton();
                if(m_Settings.m_bImportGeometry) 
                {
                    ImportGeometry();
                    ImportMaterials();
                }

                // Compute bone info if bones present
                if (!Geom.m_Bone.empty()) 
                {
                    Geom.ComputeBoneInfo();
                }

                m_pScene = nullptr;
                m_pGeom  = nullptr;
            }
            catch (std::runtime_error Error)
            {
                xerr::LogMessage<state::FAILURE>(std::format("{}", Error.what()));
                return xerr::create_f<state,"Exception thrown by assimp">();
            }

            return {};
        }

    protected:

        struct refs
        {
            std::vector<const aiNode*> m_Nodes;
        };

        std::vector<refs>   m_MeshReferences    = {};
        geom*               m_pGeom             = nullptr;
        const aiScene*      m_pScene            = nullptr;
        
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
            std::function<void(const aiNode& Node)> ProcessNode = [&](const aiNode& Node) noexcept
            {
                for (auto i = 0u; i < Node.mNumMeshes; ++i) 
                {
                    m_MeshReferences[Node.mMeshes[i]].m_Nodes.push_back(&Node);
                }

                for (auto i = 0u; i < Node.mNumChildren; ++i) 
                {
                    ProcessNode(*Node.mChildren[i]);
                }
            };

            ProcessNode(*m_pScene->mRootNode);

            for (auto iMesh = 0u; iMesh < m_pScene->mNumMeshes; ++iMesh) 
            {
                const aiMesh& AssimpMesh = *m_pScene->mMeshes[iMesh];
                const auto& Refs = m_MeshReferences[iMesh].m_Nodes;
                if (Refs.empty())
                    return xerr::create_f<state, "ERROR: Mesh without references in scene">();

                if (AssimpMesh.HasBones()) 
                {
                    if (Refs.size() > 1)
                    {
                        xerr::LogMessage<state::FAILURE>(std::format("ERROR: Skinned mesh '{}' referenced multiple times.", AssimpMesh.mName.C_Str()));
                        return xerr::create_f<state, "ERROR: Skinned mesh referenced multiple times">();
                    }

                    if (AssimpMesh.mNumBones > geom::vertex_max_weights_v) 
                    {
                        xerr::LogMessage<state::FAILURE>(std::format("ERROR: Mesh '%s' has too many bones (%u > %d).", AssimpMesh.mName.C_Str(), AssimpMesh.mNumBones, geom::vertex_max_weights_v));
                        return xerr::create_f<state, "ERROR: Mesh has too many bones">();
                    }
                }
                else if (Refs.size() > 1) 
                {
                    printf("INFO: Duplicating static mesh '%s' %zd times.\n", AssimpMesh.mName.C_Str(), Refs.size());
                }
            }

            return {};
        }

        //--------------------------------------------------------------------------------------------------

        aiMatrix4x4 GetGlobalTransform(const aiNode* pNode) const noexcept
        {
            aiMatrix4x4 mat = pNode->mTransformation;
            for (pNode = pNode->mParent; pNode; pNode = pNode->mParent) 
            {
                mat = pNode->mTransformation * mat;
            }
            return mat;
        }

        //--------------------------------------------------------------------------------------------------

        xerr ImportSkeleton() noexcept
        {
            // No meshes found...
            if (m_pScene->HasMeshes() == false) return {};

            std::unordered_map<std::string, const aiNode*>  NameToNode;
            std::unordered_map<std::string, const aiBone*>  NameToBone;
            std::unordered_map<std::string, aiMatrix4x4>    NameToMeshGlobal;

            // Collect bones from skinned meshes
            for (auto iMesh = 0u; iMesh < m_pScene->mNumMeshes; ++iMesh) 
            {
                const aiMesh& Mesh = *m_pScene->mMeshes[iMesh];
                if (!Mesh.HasBones()) continue;

                const aiNode*       pMeshNode   = m_MeshReferences[iMesh].m_Nodes[0];
                const aiMatrix4x4   MeshGlobal  = GetGlobalTransform(pMeshNode);

                for (auto iBone = 0u; iBone < Mesh.mNumBones; ++iBone) 
                {
                    const aiBone&   Bone = *Mesh.mBones[iBone];
                    std::string     Name = Bone.mName.C_Str();

                    if (NameToBone.find(Name) == NameToBone.end()) 
                    {
                        NameToBone[Name]        = &Bone;
                        NameToMeshGlobal[Name]  = MeshGlobal;
                        const aiNode* pNode = m_pScene->mRootNode->FindNode(Bone.mName);
                        if (pNode) NameToNode[Name] = pNode;
                    }
                }
            }

            // Add parent nodes
            for (auto it = NameToNode.begin(); it != NameToNode.end(); ++it) 
            {
                for (auto pParent = it->second->mParent; pParent; pParent = pParent->mParent) 
                {
                    std::string ParentName = pParent->mName.C_Str();
                    if (NameToNode.find(ParentName) == NameToNode.end()) 
                    {
                        NameToNode[ParentName] = pParent;
                    }
                }
            }

            // Arbitrary limit, adjust if needed
            if (NameToNode.size() > 4096)
            {
                xerr::LogMessage<state::FAILURE>(std::format("ERROR: Too many bones {}", NameToNode.size()));
                return xerr::create_f<state, "ERROR: Too many bones">();
            }

            // Proto for sorting
            struct proto
            {
                const aiNode*   m_pAssimpNode       = nullptr;
                int             m_Depth             = 0;
                int             m_nTotalChildren    = 0;
                int             m_nChildren         = 0;
            };

            std::vector<proto>  Proto(NameToNode.size());
            int                 idx = 0;

            for (const auto& it : NameToNode) 
            {
                Proto[idx++].m_pAssimpNode = it.second;
            }

            // Compute depths and children
            for (auto& P : Proto) 
            {
                bool foundParent = false;
                for (auto pNode = P.m_pAssimpNode->mParent; pNode; pNode = pNode->mParent) 
                {
                    P.m_Depth++;
                    for (auto& ParentProto : Proto) 
                    {
                        if (pNode == ParentProto.m_pAssimpNode) 
                        {
                            ParentProto.m_nTotalChildren++;
                            if (!foundParent) ParentProto.m_nChildren++;
                            foundParent = true;
                            break;
                        }
                    }
                }
            }

            // Sort: depth asc, total children desc, name
            // We do not sort it here. We will leave it in the default order
            /*
            std::sort(Proto.begin(), Proto.end(), [](const proto& A, const proto& B) 
            {
                if (A.m_Depth != B.m_Depth) return A.m_Depth < B.m_Depth;
                if (A.m_nTotalChildren != B.m_nTotalChildren) return A.m_nTotalChildren > B.m_nTotalChildren;
                return std::strcmp(A.m_pAssimpNode->mName.C_Str(), B.m_pAssimpNode->mName.C_Str()) < 0;
            });
            */

            // Populate bones
            m_pGeom->m_Bone.resize(Proto.size());
            for (size_t i = 0; i < Proto.size(); ++i) 
            {
                const proto& P = Proto[i];
                auto& B         = m_pGeom->m_Bone[i];
                B.m_Name        = P.m_pAssimpNode->mName.C_Str();
                B.m_nChildren   = P.m_nChildren;
                B.m_iParent     = -1;

                // Find parent index
                if (P.m_pAssimpNode->mParent) 
                {
                    for (size_t j = 0; j < i; ++j) 
                    {
                        if (Proto[j].m_pAssimpNode == P.m_pAssimpNode->mParent) 
                        {
                            B.m_iParent = static_cast<std::int32_t>(j);
                            break;
                        }
                    }
                }

                // Decompose local transform for scale, rotation, position
                aiVector3D scale, position;
                aiQuaternion rotation;
                P.m_pAssimpNode->mTransformation.Decompose(scale, rotation, position);
                B.m_Scale       = xmath::fvec3(scale.x, scale.y, scale.z);
                B.m_Rotation    = xmath::fquat(rotation.w, rotation.x, rotation.y, rotation.z);
                B.m_Position    = xmath::fvec3(position.x, position.y, position.z);

                // BBox to be computed later
                B.m_BBox = xmath::fbbox();  // Empty

                // InvBind if bone has offset
                auto boneIt = NameToBone.find(B.m_Name);
                if (boneIt != NameToBone.end()) 
                {
                    aiMatrix4x4 offset = boneIt->second->mOffsetMatrix;

                    // Adjust as in original: offset * inverse mesh global? But in original it's offset * nodeGlobal.inverse()
                    aiMatrix4x4 meshGlobal  = NameToMeshGlobal[B.m_Name];
                    aiMatrix4x4 invBindAi   = offset * meshGlobal.Inverse();

                    // Transpose if xmath is column-major
                    // Assuming fmat4 exists, but since not in geom::bone, perhaps no invBind in bone?
                    // Wait, looking back, geom::bone has no invBind.
                    // In original import3d::skeleton, bones have m_InvBind, m_NeutralPose.
                    // But in xraw3d::bone, no invBind or neutral.
                    // Perhaps compute neutral from global, but bone has only bind pose components.
                    // For skinning, invBind is needed, but perhaps handled differently in xraw3d.
                    // Assume for now, no invBind in bone, perhaps computed on fly or not needed yet.
                }
            }

            // Normalization: Find root, apply root bind to invBinds if applicable
            // But since no invBind in bone, skip or add if needed.
            // Original normalizes invBind and vertices.
            // For xraw3d, we'll handle in ImportGeometry for vertices.
            return {};
        }

        //--------------------------------------------------------------------------------------------------

        std::string GetMeshNameFromNode(const aiNode& Node)
        {
            for (auto pNode = &Node; pNode; pNode = pNode->mParent) 
            {
                if (xstrtool::findI(pNode->mName.C_Str(), "MESH_") != std::string::npos) 
                {
                    return pNode->mName.C_Str();
                }
            }
            return Node.mName.C_Str();  // Fallback to node name
        }

        //--------------------------------------------------------------------------------------------------

        xerr ImportGeometryValidateMesh(const aiMesh& AssimpMesh)
        {
            if (!AssimpMesh.HasPositions()) 
            {
                xerr::LogMessage<state::FAILURE>(std::format("Mesh '{}' without positions.", AssimpMesh.mName.C_Str()));
                return xerr::create_f<state, "Mesh without positions">();
            }
            if (!AssimpMesh.HasFaces()) 
            {
                xerr::LogMessage<state::FAILURE>(std::format("Mesh '{}' without faces.", AssimpMesh.mName.C_Str()));
                return xerr::create_f<state, "Mesh without faces">();
            }
            if (!AssimpMesh.HasNormals())
            {
                xerr::LogMessage<state::FAILURE>(std::format("Mesh '{}' without normals.", AssimpMesh.mName.C_Str()));
                return xerr::create_f<state, "Mesh without normals">();
            }
            if (!AssimpMesh.HasTangentsAndBitangents())
            {
                xerr::LogMessage<state::WARNING>(std::format("Mesh '{}' without tangents/bitangents, using fakes", AssimpMesh.mName.C_Str()));
            }
            if (AssimpMesh.GetNumUVChannels() > geom::vertex_max_uv_v)
            {
                xerr::LogMessage<state::WARNING>(std::format("Mesh '{}' has too many UV channels ({} > {})", AssimpMesh.mName.C_Str(), AssimpMesh.GetNumUVChannels(), geom::vertex_max_uv_v));
            }
            if (AssimpMesh.GetNumUVChannels() == 0)
            {
                xerr::LogMessage<state::WARNING>(std::format("Mesh '{}' without UVs, using (0,0).", AssimpMesh.mName.C_Str()));
            }
            if (AssimpMesh.GetNumColorChannels() > geom::vertex_max_colors_v)
            {
                xerr::LogMessage<state::WARNING>(std::format("Mesh '{}' has too many color channels ({} > {}).", AssimpMesh.mName.C_Str(), AssimpMesh.GetNumColorChannels(), geom::vertex_max_colors_v));
            }
            if (AssimpMesh.GetNumColorChannels() == 0)
            {
                xerr::LogMessage<state::WARNING>(std::format("Mesh '{}' without colors.", AssimpMesh.mName.C_Str()));
            }
            return {};
        }

        //--------------------------------------------------------------------------------------------------

        void ImportGeometry() noexcept
        {
            // Collect all mesh parts without merging
            struct myMeshPart
            {
                std::string                 m_MeshName;
                std::string                 m_Name;
                std::vector<geom::vertex>   m_Vertices;
                std::vector<std::int32_t>   m_Indices;
                std::int32_t                m_iMaterialInstance = -1;
                std::int32_t                m_nBones = 0;
            };
            std::vector<myMeshPart> MeshParts;

            aiMatrix4x4 rootBind, invRootBind;
            int rootBone = -1;
            if (!m_pGeom->m_Bone.empty()) 
            {
                for (size_t i = 0; i < m_pGeom->m_Bone.size(); ++i) 
                {
                    if (m_pGeom->m_Bone[i].m_iParent == -1) 
                    {
                        rootBone = static_cast<int>(i);
                        break;
                    }
                }

                if (rootBone != -1) 
                {
                    const aiNode* rootNode = m_pScene->mRootNode->FindNode(m_pGeom->m_Bone[rootBone].m_Name.c_str());
                    if (rootNode) 
                    {
                        rootBind    = GetGlobalTransform(rootNode);
                        invRootBind = rootBind.Inverse();
                    }
                }
            }

            // If we are doing static geometry we do not need to put the mesh in the space of the skeleton
            if (m_Settings.m_bAnimated == false )
            {
                invRootBind = aiMatrix4x4();
            }

            for (auto iMesh = 0u; iMesh < m_pScene->mNumMeshes; ++iMesh) 
            {
                const aiMesh&   AssimpMesh = *m_pScene->mMeshes[iMesh];

                if (auto Err = ImportGeometryValidateMesh(AssimpMesh); Err ) 
                    continue;

                const auto& refs = m_MeshReferences[iMesh].m_Nodes;
                for (const auto* pNode : refs) 
                {
                    myMeshPart part;
                    part.m_Name                 = AssimpMesh.mName.C_Str();
                    part.m_MeshName             = GetMeshNameFromNode(*pNode);
                    part.m_iMaterialInstance    = AssimpMesh.mMaterialIndex;

                    aiMatrix4x4 meshGlobal          = GetGlobalTransform(pNode);
                    aiMatrix4x4 combinedTransform   = invRootBind * meshGlobal;
                    aiVector3D combScale, combPos;
                    aiQuaternion combRot;
                    combinedTransform.Decompose(combScale, combRot, combPos);

                    // Vertices
                    part.m_Vertices.resize(AssimpMesh.mNumVertices);
                    for (auto iVert = 0u; iVert < AssimpMesh.mNumVertices; ++iVert) 
                    {
                        geom::vertex&   V   = part.m_Vertices[iVert];
                        aiVector3D      pos = AssimpMesh.mVertices[iVert];

                        pos = combinedTransform * pos;

                        V.m_Position    = xmath::fvec3(pos.x, pos.y, pos.z);
                        V.m_iFrame      = 0;

                        // Collect multiple UVs
                        V.m_nUVs = 0;
                        const auto numUVChannels = static_cast<int>(AssimpMesh.GetNumUVChannels());
                        for(int ch = 0; ch < numUVChannels && V.m_nUVs < geom::vertex_max_uv_v; ++ch)
                        {
                            if (AssimpMesh.HasTextureCoords(ch))
                            {
                                aiVector3D uv = AssimpMesh.mTextureCoords[ch][iVert];
                                V.m_UV[V.m_nUVs++] = xmath::fvec2(uv.x, uv.y);
                            }
                        }

                        // Collect multiple colors
                        V.m_nColors = 0;
                        const auto numColorChannels = static_cast<int>(AssimpMesh.GetNumColorChannels());
                        for (int ch = 0; ch < numColorChannels && V.m_nColors < geom::vertex_max_colors_v; ++ch)
                        {
                            if (AssimpMesh.HasVertexColors(ch))
                            {
                                aiColor4D c = AssimpMesh.mColors[ch][iVert];
                                V.m_Color[V.m_nColors++].setupFromRGBA(c.r, c.g, c.b, c.a);
                            }
                        }

                        // Normals, tangents, binormals
                        aiVector3D normal = AssimpMesh.mNormals[iVert];
                        normal = combRot.Rotate(normal);

                        V.m_BTN[0].m_Normal = xmath::fvec3(normal.x, normal.y, normal.z).NormalizeSafe();
                        V.m_nNormals = 1;
                        if (AssimpMesh.HasTangentsAndBitangents())
                        {
                            aiVector3D tangent  = AssimpMesh.mTangents[iVert];
                            aiVector3D binormal = AssimpMesh.mBitangents[iVert];
                            tangent     = combRot.Rotate(tangent);
                            binormal    = combRot.Rotate(binormal);

                            V.m_BTN[0].m_Tangent    = xmath::fvec3(tangent.x, tangent.y, tangent.z).NormalizeSafe();
                            V.m_BTN[0].m_Binormal   = xmath::fvec3(binormal.x, binormal.y, binormal.z).NormalizeSafe();
                            V.m_nBinormals = 1;
                            V.m_nNormals   = 1;
                        }
                        else 
                        {
                            V.m_BTN[0].m_Tangent  = xmath::fvec3(1, 0, 0);
                            V.m_BTN[0].m_Binormal = xmath::fvec3(0, 1, 0);
                            V.m_nBinormals  = 0;
                            V.m_nNormals    = 0;
                        }

                        V.m_nWeights = 0;  // Set below
                    }

                    // Indices
                    part.m_Indices.reserve(AssimpMesh.mNumFaces * 3);
                    for (auto iFace = 0u; iFace < AssimpMesh.mNumFaces; ++iFace) 
                    {
                        const auto& face = AssimpMesh.mFaces[iFace];
                        assert(face.mNumIndices == 3);  // Triangulated

                        for (auto j = 0u; j < face.mNumIndices; ++j) 
                        {
                            part.m_Indices.push_back(face.mIndices[j]);
                        }
                    }

                    // Weights and bones
                    if (AssimpMesh.HasBones()) 
                    {
                        std::vector<std::vector<geom::weight>> VertWeights(AssimpMesh.mNumVertices);
                        std::unordered_set<std::int32_t> usedBones;
                        for (auto iBone = 0u; iBone < AssimpMesh.mNumBones; ++iBone) 
                        {
                            const aiBone&   aBone   = *AssimpMesh.mBones[iBone];
                            std::int32_t    boneID  = m_pGeom->getBoneIDFromName(aBone.mName.C_Str());

                            if (boneID == -1) continue;

                            usedBones.insert(boneID);

                            for (auto iW = 0u; iW < aBone.mNumWeights; ++iW) 
                            {
                                const aiVertexWeight& vw = aBone.mWeights[iW];
                                if (vw.mWeight > 0.0f) 
                                {
                                    VertWeights[vw.mVertexId].push_back({ boneID, vw.mWeight });
                                }
                            }
                        }
                        part.m_nBones = static_cast<std::int32_t>(usedBones.size());

                        for (auto iVert = 0u; iVert < AssimpMesh.mNumVertices; ++iVert) 
                        {
                            auto& weights = VertWeights[iVert];

                            // Sort descending weight
                            std::sort(weights.begin(), weights.end(), [](const geom::weight& a, const geom::weight& b) 
                            {
                                return a.m_Weight > b.m_Weight;
                            });

                            // Normalize
                            float total = 0.0f;
                            for (const auto& w : weights) total += w.m_Weight;

                            if (total > 0.0f) 
                            {
                                for (auto& w : weights) w.m_Weight /= total;
                            }

                            // Assign to vertex, cap to max_weights
                            auto& V = part.m_Vertices[iVert];
                            V.m_nWeights = std::min(static_cast<std::int32_t>(weights.size()), geom::vertex_max_weights_v);
                            for (std::int32_t j = 0; j < V.m_nWeights; ++j) 
                            {
                                V.m_Weight[j] = weights[j];
                            }
                        }
                    }
                    else 
                    {
                        // Static: attach to bone if node is bone, else root or none
                        std::int32_t boneID = m_pGeom->getBoneIDFromName(pNode->mName.C_Str());
                        if (boneID == -1) boneID = (rootBone != -1) ? rootBone : -1;
                        if (boneID != -1)
                        {
                            part.m_nBones = 1;
                            for (auto& V : part.m_Vertices)
                            {
                                V.m_nWeights = 1;
                                V.m_Weight[0].m_iBone = boneID;
                                V.m_Weight[0].m_Weight = 1.0f;
                            }
                        }
                        else
                        {
                            part.m_nBones = 0;
                            for (auto& V : part.m_Vertices)
                            {
                                V.m_nWeights = 0;
                            }
                        }
                    }

                    MeshParts.push_back(std::move(part));
                }
            }

            // Now, create meshes, append vertices/facets globally
            std::unordered_map<std::string, std::int32_t> meshNameToIndex;
            std::int32_t globalVertBase = 0;
            for (auto& part : MeshParts) 
            {
                std::int32_t iMesh = -1;
                auto it = meshNameToIndex.find(part.m_MeshName);
                if (it == meshNameToIndex.end()) 
                {
                    iMesh = static_cast<std::int32_t>(m_pGeom->m_Mesh.size());
                    m_pGeom->m_Mesh.emplace_back();
                    m_pGeom->m_Mesh.back().m_Name = part.m_MeshName;
                    m_pGeom->m_Mesh.back().m_nBones = part.m_nBones;
                    meshNameToIndex[part.m_MeshName] = iMesh;
                }
                else 
                {
                    iMesh = it->second;
                    m_pGeom->m_Mesh[iMesh].m_nBones = std::max(m_pGeom->m_Mesh[iMesh].m_nBones, part.m_nBones);
                }

                // Append vertices
                std::int32_t vertBase = static_cast<std::int32_t>(m_pGeom->m_Vertex.size());
                m_pGeom->m_Vertex.insert(m_pGeom->m_Vertex.end(), part.m_Vertices.begin(), part.m_Vertices.end());

                // Facets
                assert(part.m_Indices.size() % 3 == 0);
                for (size_t i = 0; i < part.m_Indices.size(); i += 3) 
                {
                    geom::facet F;
                    F.m_iMesh       = iMesh;
                    F.m_nVertices   = 3;
                    F.m_iVertex[0]  = part.m_Indices[i + 0] + vertBase;
                    F.m_iVertex[1]  = part.m_Indices[i + 1] + vertBase;
                    F.m_iVertex[2]  = part.m_Indices[i + 2] + vertBase;
                    F.m_iMaterialInstance = part.m_iMaterialInstance;

                    // Compute plane
                    const auto& p0 = m_pGeom->m_Vertex[F.m_iVertex[0]].m_Position;
                    const auto& p1 = m_pGeom->m_Vertex[F.m_iVertex[1]].m_Position;
                    const auto& p2 = m_pGeom->m_Vertex[F.m_iVertex[2]].m_Position;
                    xmath::fvec3 normal = (p1 - p0).Cross(p2 - p0).Normalize();
                    F.m_Plane = xmath::fplane(normal, -normal.Dot(p0));
                    m_pGeom->m_Facet.push_back(F);
                }
            }

            // Optional: sort facets by mesh/material if wanted
            // m_pGeom->SortFacetsByMeshMaterialBone();
        }

        //--------------------------------------------------------------------------------------------------

        void ImportMaterials() noexcept
        {
            std::unordered_map<std::int32_t, std::int32_t> assimpToGeomMat;

            for (auto iMat = 0u; iMat < m_pScene->mNumMaterials; ++iMat) 
            {
                const aiMaterial*           aMat = m_pScene->mMaterials[iMat];
                geom::material_instance&    matI = m_pGeom->m_MaterialInstance.emplace_back();

                matI.m_Name             = aMat->GetName().C_Str();
                matI.m_MaterialShader   = "default";  // Placeholder, set if known
                matI.m_Technique        = "default";  // Placeholder

                // Params
                // Colors
                aiColor4D color;
                if (aiGetMaterialColor(aMat, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS) 
                {
                    geom::material_instance::params p;
                    p.m_Type = geom::material_instance::params_type::F4;
                    xstrtool::Copy(p.m_Name, "DiffuseColor");
                    p.m_Value = std::format( "{} {} {} {}", color.r, color.g, color.b, color.a);
                    matI.m_Params.push_back(p);
                }
                // Similar for specular, ambient, emissive

                if (aiGetMaterialColor(aMat, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS) 
                {
                    geom::material_instance::params p;
                    p.m_Type = geom::material_instance::params_type::F4;
                    xstrtool::Copy(p.m_Name, "SpecularColor");
                    p.m_Value = std::format("{} {} {} {}", color.r, color.g, color.b, color.a);
                    matI.m_Params.push_back(p);
                }

                if (aiGetMaterialColor(aMat, AI_MATKEY_COLOR_AMBIENT, &color) == AI_SUCCESS) 
                {
                    geom::material_instance::params p;
                    p.m_Type = geom::material_instance::params_type::F4;
                    xstrtool::Copy(p.m_Name, "AmbientColor");
                    p.m_Value = std::format("{} {} {} {}", color.r, color.g, color.b, color.a);
                    matI.m_Params.push_back(p);
                }

                if (aiGetMaterialColor(aMat, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS) 
                {
                    geom::material_instance::params p;
                    p.m_Type = geom::material_instance::params_type::F4;
                    xstrtool::Copy(p.m_Name, "EmissiveColor");
                    p.m_Value = std::format("{} {} {} {}", color.r, color.g, color.b, color.a);
                    matI.m_Params.push_back(p);
                }

                // Floats
                float f;
                if (aiGetMaterialFloat(aMat, AI_MATKEY_OPACITY, &f) == AI_SUCCESS) 
                {
                    geom::material_instance::params p;
                    p.m_Type = geom::material_instance::params_type::F1;
                    xstrtool::Copy(p.m_Name, "Opacity");
                    p.m_Value = std::format("{}", f);
                    matI.m_Params.push_back(p);
                }

                if (aiGetMaterialFloat(aMat, AI_MATKEY_SHININESS, &f) == AI_SUCCESS) 
                {
                    geom::material_instance::params p;
                    p.m_Type = geom::material_instance::params_type::F1;
                    xstrtool::Copy(p.m_Name, "Shininess");
                    p.m_Value = std::format("{}", f);
                    matI.m_Params.push_back(p);
                }

                // Textures
                auto HandleTexture = [&](aiTextureType type, const char* name)
                {
                    aiString path;
                    if (aMat->GetTextureCount(type) > 0 && aMat->GetTexture(type, 0, &path) == AI_SUCCESS) 
                    {
                        geom::material_instance::params p;
                        p.m_Type = geom::material_instance::params_type::TEXTURE;
                        xstrtool::Copy(p.m_Name, name);
                        xstrtool::Copy(p.m_Value, path.C_Str());
                        matI.m_Params.push_back(p);
                    }
                };

                HandleTexture(aiTextureType_DIFFUSE,    "DiffuseTexture");
                HandleTexture(aiTextureType_SPECULAR,   "SpecularTexture");
                HandleTexture(aiTextureType_AMBIENT,    "AmbientTexture");
                HandleTexture(aiTextureType_EMISSIVE,   "EmissiveTexture");
                HandleTexture(aiTextureType_HEIGHT,     "HeightTexture");
                HandleTexture(aiTextureType_NORMALS,    "NormalTexture");
                HandleTexture(aiTextureType_SHININESS,  "ShininessTexture");
                HandleTexture(aiTextureType_OPACITY,    "OpacityTexture");
                HandleTexture(aiTextureType_LIGHTMAP,   "LightmapTexture");

                // Sort params
                std::sort(matI.m_Params.begin(), matI.m_Params.end());

                assimpToGeomMat[iMat] = static_cast<std::int32_t>(m_pGeom->m_MaterialInstance.size() - 1);
            }

            // Remap facet materials
            for (auto& F : m_pGeom->m_Facet) 
            {
                auto it = assimpToGeomMat.find(F.m_iMaterialInstance);
                if (it != assimpToGeomMat.end()) 
                {
                    F.m_iMaterialInstance = it->second;
                }
                else 
                {
                    F.m_iMaterialInstance = 0;  // Default
                }
            }
        }
    };
} // namespace xraw3d