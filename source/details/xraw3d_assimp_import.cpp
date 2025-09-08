
#include "dependencies/assimp/include/assimp/Importer.hpp"
#include "dependencies/assimp/include/assimp/scene.h"
#include "dependencies/assimp/include/assimp/postprocess.h"

#pragma message("**xraw3d_assimp_import.cpp** is adding assimp-vc143-mt.lib")
#pragma comment( lib, "dependencies/assimp/BINARIES/Win32/lib/Release/assimp-vc143-mt.lib" )

namespace xraw3d::assimp {

struct texture 
{
    std::string     m_Type;
    std::string     m_Path;
};

//--------------------------------------------------------------------------------------

struct importer_mesh
{
    std::string                 m_Name;
    std::vector<geom::vertex>   m_Vertices;
    std::vector<geom::facet>    m_Facets;
    int                         m_iMaterial;
    int                         m_nBones;
};

//--------------------------------------------------------------------------------------

struct importer
{
    importer( anim& Anim, geom& Geom )
        : m_RawAnim{ Anim }
        , m_RawGeom{ Geom }
        {}

    //--------------------------------------------------------------------------------------

    void Load( std::string FileName )
    {
        m_pScene = m_Assimp.ReadFile( FileName
                                    , aiProcess_Triangulate                // Make sure we get triangles rather than nvert polygons
                                    | aiProcess_LimitBoneWeights           // 4 weights for skin model max
                                    | aiProcess_GenUVCoords                // Convert any type of mapping to uv mapping
                                    | aiProcess_TransformUVCoords          // preprocess UV transformations (scaling, translation ...)
                                    | aiProcess_FindInstances              // search for instanced meshes and remove them by references to one master
                                    | aiProcess_CalcTangentSpace           // calculate tangents and bitangents if possible
                                    | aiProcess_JoinIdenticalVertices      // join identical vertices/ optimize indexing
                                    | aiProcess_RemoveRedundantMaterials   // remove redundant materials
                                    | aiProcess_FindInvalidData            // detect invalid model data, such as invalid normal vectors
                                    );
        if (m_pScene == nullptr)
            throw( std::runtime_error( "Fail to get the main scene" ));

        m_Directory = FileName.substr(0, FileName.find_last_of("/\\"));
    }

    //--------------------------------------------------------------------------------------

    void ImportGeometry( void )
    {
        //
        // Import all the geometry
        //
        ProcessNode( *m_pScene->mRootNode, *m_pScene );

        //
        // Allocate verts + facets
        //
        {
            std::size_t VertexCount = 0;
            std::size_t FaceCount   = 0;
            for( auto& ImporterMesh : m_ImporterMeshes )
            {
                VertexCount += ImporterMesh.m_Vertices.size();
                FaceCount   += ImporterMesh.m_Facets.size();
            }

            m_RawGeom.m_Vertex.resize(VertexCount);
            m_RawGeom.m_Facet.resize(FaceCount);
        }

        //
        // Copy verts + facets
        //
        {
            int iVert = 0;
            int iFace = 0;
            for( auto& ImporterMesh : m_ImporterMeshes )
            {
                const int iVertBase = iVert;

                // Copy the verts directly
                for( auto& Vert : ImporterMesh.m_Vertices )
                    m_RawGeom.m_Vertex[iVert++] = Vert;

                // Copy the face and update the indices to index into the global pool of vertices
                for (auto& Face : ImporterMesh.m_Facets )
                {
                    auto& GeomFacet = m_RawGeom.m_Facet[iFace++];
                    GeomFacet = Face;
                    for( int i=0; i< GeomFacet.m_nVertices; ++i )
                    {
                        GeomFacet.m_iVertex[i] += iVertBase;
                    }
                }
            }
        }

        //
        // Copy the meshes
        //
        {
            m_RawGeom.m_Mesh.resize( m_ImporterMeshes.size() );

            for( auto i=0u; i<m_ImporterMeshes.size(); ++i )
            {
                auto& Mesh      = m_ImporterMeshes[i];
                auto& GeomMesh  = m_RawGeom.m_Mesh[i];
                
                GeomMesh.m_Name   = Mesh.m_Name;
                GeomMesh.m_nBones = Mesh.m_nBones;
            }
        }

        //
        // Create the material slots
        //
        if( m_pScene->HasMaterials() )
        {
            m_RawGeom.m_MaterialInstance.resize(m_pScene->mNumMaterials);

            for( auto i=0u; i<m_pScene->mNumMaterials; ++i )
            {
                m_RawGeom.m_MaterialInstance[i].m_Name = std::format("Materials-{}", i);
            }
        }

        // TODO: 
        // m_RawGeom.m_MaterialInstance;
        // m_RawGeom.m_Bone

    }
    
    // CollectBones( *pScene->mRootNode );

    //--------------------------------------------------------------------------------------

    void ProcessNode( const aiNode& Node, const aiScene& Scene )
    {
        //
        // Collect all the meshes
        //
        for( auto i = 0u, end = Node.mNumMeshes; i < end; ++i )
        {
            aiMesh* pMesh = Scene.mMeshes[Node.mMeshes[i]];
            m_ImporterMeshes.push_back( ProcessMesh( *pMesh, Scene ) );
        }

        //
        // Recurse to process other nodes in search for meshes
        //
        for (auto i = 0u; i < Node.mNumChildren; ++i)
        {
            ProcessNode(*Node.mChildren[i], Scene);
        }
    }

    //--------------------------------------------------------------------------------------

    importer_mesh ProcessMesh( const aiMesh& AssimpMesh, const aiScene& AssimpScene ) noexcept
    {
        // Data to fill
        importer_mesh ImporterMesh;

        // Preallocate some memory
        ImporterMesh.m_Vertices.reserve(AssimpMesh.mNumVertices );
        ImporterMesh.m_Facets.reserve(AssimpMesh.mNumFaces );

        //
        // Walk through each of the importer_mesh's vertices
        //
        for( auto i = 0u; i < AssimpMesh.mNumVertices; ++i )
        {
            geom::vertex Vertex;

            Vertex.m_Position = xmath::fvec3
            ( static_cast<float>(AssimpMesh.mVertices[i].x)
            , static_cast<float>(AssimpMesh.mVertices[i].y)
            , static_cast<float>(AssimpMesh.mVertices[i].z)
            );

            if( AssimpMesh.HasTextureCoords(0) )
            {
                Vertex.m_nUVs = AssimpMesh.GetNumUVChannels();
                for( int iuv=0; iuv < Vertex.m_nUVs; ++iuv)
                {
                    Vertex.m_UV[iuv] = xmath::fvec2( static_cast<float>(AssimpMesh.mTextureCoords[iuv][i].x)
                                                   , static_cast<float>(AssimpMesh.mTextureCoords[iuv][i].y)
                                                   );
                }
            }
            else
            {
                Vertex.m_nUVs = 0;
            }

            if( AssimpMesh.HasVertexColors(0) )
            {
                Vertex.m_nColors = AssimpMesh.GetNumColorChannels();
                for( int icolor = 0; icolor < Vertex.m_nColors; icolor++ )
                {
                    xmath::fvec4 RGBA ( static_cast<float>(AssimpMesh.mColors[icolor][i].r)
                                      , static_cast<float>(AssimpMesh.mColors[icolor][i].g)
                                      , static_cast<float>(AssimpMesh.mColors[icolor][i].b)
                                      , static_cast<float>(AssimpMesh.mColors[icolor][i].a)
                                      );

                    Vertex.m_Color[icolor].setupFromRGBA( RGBA );

                }
            }
            else
            {
                Vertex.m_nColors = 0;
            }

            if(AssimpMesh.HasNormals() )
            {
                Vertex.m_nNormals = 1;
                Vertex.m_BTN[0].m_Normal = xmath::fvec3(AssimpMesh.mNormals[i].x, AssimpMesh.mNormals[i].y, AssimpMesh.mNormals[i].z );
            }
            else
            {
                Vertex.m_nNormals = 0;
            }

            if(AssimpMesh.HasTangentsAndBitangents() )
            {
                Vertex.m_nTangents  = 1;
                Vertex.m_nBinormals = 1;
                Vertex.m_BTN[0].m_Tangent  = xmath::fvec3(AssimpMesh.mTangents[i].x, AssimpMesh.mTangents[i].y, AssimpMesh.mTangents[i].z  );
                Vertex.m_BTN[0].m_Binormal = xmath::fvec3(AssimpMesh.mBitangents[i].x, AssimpMesh.mBitangents[i].y, AssimpMesh.mBitangents[i].z);
            }
            else
            {
                Vertex.m_nTangents  = 0;
                Vertex.m_nBinormals = 0;
            }

            // Zero out the weights for now
            Vertex.m_nWeights = 0;

            ImporterMesh.m_Vertices.push_back(Vertex);
        }

        //
        // Walk through each of the importer_mesh's bones
        //
        for( auto iBone = 0u; iBone< AssimpMesh.mNumBones; iBone++ )
        {
            auto& AssimpBone = *AssimpMesh.mBones[iBone];

            for( auto iWeight = 0u; iWeight < AssimpBone.mNumWeights; ++iWeight )
            {
                auto& AssimpWeight = AssimpBone.mWeights[iWeight];
                auto& GeomVert     = ImporterMesh.m_Vertices[ AssimpWeight.mVertexId ];
                auto& GeomWeight   = GeomVert.m_Weight[GeomVert.m_nWeights++];

                GeomWeight.m_iBone  = iBone;
                GeomWeight.m_Weight = AssimpWeight.mWeight;
            }
        }

        //
        // Walk thourgh the faces
        //
        for( auto i = 0u; i < AssimpMesh.mNumFaces; ++i )
        {
            const auto& Face = AssimpMesh.mFaces[i];
            geom::facet GeomFace;

            GeomFace.m_nVertices         = 3;
            GeomFace.m_iMaterialInstance = AssimpMesh.mMaterialIndex;
            GeomFace.m_iMesh             = int(m_ImporterMeshes.size());

            for( auto j = 0u; j < Face.mNumIndices; ++j )
            {
                GeomFace.m_iVertex[j] = Face.mIndices[j];
            }

            ImporterMesh.m_Facets.push_back(GeomFace);
        }

        //
        // Handle materials
        //
        if( AssimpMesh.mMaterialIndex >= 0 )
        {
            ImporterMesh.m_iMaterial   = AssimpMesh.mMaterialIndex;
        }

        //
        // Copy importer_mesh related info
        //
        ImporterMesh.m_nBones = AssimpMesh.mNumBones;
        ImporterMesh.m_Name   = AssimpMesh.mName.C_Str();

        // Done
        return ImporterMesh;
    }

    //--------------------------------------------------------------------------------------

    anim&                       m_RawAnim;
    geom&                       m_RawGeom;
    const aiScene*              m_pScene        { nullptr };
    Assimp::Importer            m_Assimp        {};
    std::string                 m_Directory     {};
    std::vector<importer_mesh>  m_ImporterMeshes{};
};

//--------------------------------------------------------------------------------------

void ImportAll( anim& Anim, geom& Geom, const char* pFileName )
{
    auto Importer = std::make_unique<importer>(Anim, Geom);
    Importer->Load( pFileName );
    Importer->ImportGeometry();
}

} //namespace xraw3d::assimp