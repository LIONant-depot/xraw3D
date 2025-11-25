namespace xraw3d
{
    class geom
    {
    public:

        static constexpr auto vertex_max_uv_v               = 8;
        static constexpr auto vertex_max_normals_v          = 3;
        static constexpr auto vertex_max_colors_v           = 4;
        static constexpr auto vertex_max_weights_v          = 16;
        static constexpr auto facet_max_vertices_v          = 8;
        static constexpr auto material_max_maps_v           = 16;
        static constexpr auto material_max_constants_v      = 8;
        static constexpr auto param_pkg_max_items_v         = 4;

        struct bone
        {
            std::string                                     m_Name;
            std::int32_t                                    m_nChildren;
            std::int32_t                                    m_iParent;
            xmath::fvec3                                    m_Scale;
            xmath::fquat                                    m_Rotation;
            xmath::fvec3                                    m_Position;
            xmath::fbbox                                    m_BBox;
        };

        struct mesh
        {
            std::string                                     m_ScenePath;    // Full path included nodes and such
            std::string                                     m_Name;         // Just the basic name
            std::int32_t                                    m_nBones;
        };

        struct weight
        {
            std::int32_t                                    m_iBone;
            float                                           m_Weight;
        };

        struct btn
        {
            xmath::fvec3                                    m_Binormal;
            xmath::fvec3                                    m_Tangent;
            xmath::fvec3                                    m_Normal;
        };

        struct vertex
        {
            xmath::fvec3                                    m_Position;

            std::int32_t                                    m_iFrame        = 0;
            std::int32_t                                    m_nWeights      = 0;
            std::int32_t                                    m_nNormals      = 0;
            std::int32_t                                    m_nTangents     = 0;
            std::int32_t                                    m_nBinormals    = 0;
            std::int32_t                                    m_nUVs          = 0;
            std::int32_t                                    m_nColors       = 0;

            std::array<xmath::fvec2,  vertex_max_uv_v>      m_UV;
            std::array<xcolori,       vertex_max_colors_v>  m_Color;
            std::array<weight,        vertex_max_weights_v> m_Weight;
            std::array<btn,           vertex_max_normals_v> m_BTN;
        };

        struct facet
        {
            std::int32_t                                    m_iMesh;
            std::int32_t                                    m_nVertices;
            std::array<std::int32_t,facet_max_vertices_v>   m_iVertex;
            std::int32_t                                    m_iMaterialInstance;
            xmath::fplane                                   m_Plane;
        };

        // Material refers to a material instance not a material-shader/type
        // The material instance has a reference of the material shader such multiple instances
        // could in refer to the same material shader. Ideally a mesh should just point to
        // an actual material instance so that hopefully many meshes use a single instance.
        // this will the best case in terms of performance. Because all the meshes that can be
        // render with the game material instance don't need any state changes except for vertex/index buffers.
        // However most games dont really use this concept very much and most meshes have their own material instances,
        // with custom textures and tweaked parameters.
        struct material_instance
        {
            enum class params_type : std::uint8_t
            { INVALID
            , BOOL
            , F1
            , F2
            , F3
            , F4
            , TEXTURE
            , ENUM_COUNT
            };

            static auto& getTypeString( params_type e )
            {
                static constexpr auto strings_v = std::array
                { std::string_view{ "NULL"    }
                , std::string_view{ "BOOL"    }
                , std::string_view{ "F1"      }
                , std::string_view{ "F2"      }
                , std::string_view{ "F3"      }
                , std::string_view{ "F4"      }
                , std::string_view{ "TEXTURE" }
                };
                return strings_v[ static_cast<int>(e) ];
            }

            struct params
            {
                bool operator < ( const params& B ) const
                {
                    if (m_Name.empty() )   return B.m_Name.empty() ? 0 : 1;
                    if (B.m_Name.empty())  return 0;

                    std::int32_t Answer = std::strcmp( m_Name.c_str(), B.m_Name.c_str() );
                    return Answer <= 0;
                }

                params_type                             m_Type;
                std::string                             m_Name;
                std::string                             m_Value;
            };

            material_instance& operator = ( const material_instance& Material )
            {
                if ( this != &Material )
                {
                    m_Name           = Material.m_Name;
                    m_MaterialShader = Material.m_MaterialShader;
                    m_Technique      = Material.m_Technique;
                    m_Params         = Material.m_Params;
                }
                return *this;
            }

            std::string                                 m_Name;
            std::string                                 m_MaterialShader;
            std::string                                 m_Technique;
            std::vector<params>                         m_Params;
        };

    public:

        void                    Serialize                   ( bool                          isRead
                                                            , std::wstring_view             FileName
                                                            , xtextfile::file_type          Type      = xtextfile::file_type::BINARY
                                                            );
        void                    Kill                        ( void 
                                                            );
        void                    SanityCheck                 ( void 
                                                            ) const;
        void                    CleanMesh                   ( std::int32_t                  iSubMesh = -1 
                                                            );  
        void                    CleanWeights                ( std::int32_t                  MaxNumWeights
                                                            , float                         MinWeightValue 
                                                            );
        void                    ForceAddColorIfNone         ( void 
                                                            );
        void                    CollapseMeshes              ( std::string_view              MeshName 
                                                            );
        void                    CollapseNormals             ( xmath::radian                 ThresholdAngle = xmath::radian(xmath::DegToRad(20.0f))
                                                            );
        xmath::fbbox            getBBox                     ( void 
                                                            ) const;
        void                    DeleteMesh                  ( int iMesh
                                                            ) noexcept;

        void                    ComputeMeshBBox             ( std::int32_t                  iMesh
                                                            , xmath::fbbox&                 BBox 
                                                            );
        void                    ComputeBoneInfo             ( void 
                                                            );
        bool                    IsolateMesh                 ( std::int32_t                  iSubmesh
                                                            , geom&                         NewMesh
                                                            , bool                          RemoveFromRawMesh = false 
                                                            );
        bool                    IsolateMesh                 ( std::string_view              MeshName
                                                            , geom&                         NewMesh 
                                                            );
        bool                    isBoneUsed                  ( std::int32_t                  iBone 
                                                            );
        std::int32_t            getBoneIDFromName           ( std::string_view              BoneName 
                                                            ) const;
        void                    DeleteBone                  ( std::int32_t                  iBone 
                                                            );
        void                    DeleteBone                  ( std::string_view              BoneName 
                                                            );        
        void                    ApplyNewSkeleton            ( const anim&                   RawAnim 
                                                            );
        void                    ApplyNewSkeleton            ( const geom&                   Skel 
                                                            );
        void                    SortFacetsByMaterial        ( void 
                                                            );
        void                    SortFacetsByMeshMaterialBone( void 
                                                            );
        static bool             TempVCompare                ( const geom::vertex&           A
                                                            , const geom::vertex&           B
                                                            , const float                   PositionEpsilon
                                                            );
        static bool             CompareFaces                ( const geom::facet&            A
                                                            , const geom::facet&            B 
                                                            );
        int                     findMeshByName              ( std::string_view MeshName 
                                                            );
        int                     findMeshByPath              (std::string_view MeshScenePath
                                                            );

    public:

        std::vector<bone>                  m_Bone;
        std::vector<vertex>                m_Vertex;
        std::vector<facet>                 m_Facet;
        std::vector<material_instance>     m_MaterialInstance;
        std::vector<mesh>                  m_Mesh;
    };

} // xraw3d