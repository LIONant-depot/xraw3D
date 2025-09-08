namespace xraw3d
{
    class anim
    {
    public:
        
        static constexpr auto num_event_strings_v = 5;
        static constexpr auto num_event_ints_v    = 5;
        static constexpr auto num_event_floats_v  = 8;
        static constexpr auto num_event_bools_v   = 8;
        static constexpr auto num_event_colors_v  = 4;
        
        struct bone
        {
            std::string             m_Name;
            std::int32_t            m_iParent;
            std::int32_t            m_nChildren;

            xmath::fvec3            m_BindTranslation;
            xmath::fquat            m_BindRotation;
            xmath::fvec3            m_BindScale;

            bool                    m_bScaleKeys;
            bool                    m_bRotationKeys;
            bool                    m_bTranslationKeys;
            bool                    m_bIsMasked;

            xmath::fmat4            m_BindMatrix;
            xmath::fmat4            m_BindMatrixInv;
        };

        using key_frame = xmath::transform3;

        struct event
        {
            std::string             m_Name;
            std::string             m_ParentName;
            std::int32_t            m_Type;
            float                   m_Radius;
            std::int32_t            m_Frame0;
            std::int32_t            m_Frame1;
            xmath::fvec3            m_Position;
        };

        struct super_event
        {
            std::string             m_Name;

            std::int32_t            m_Type;
            std::int32_t            m_StartFrame;
            std::int32_t            m_EndFrame;
            xmath::fvec3            m_Position;
            xmath::fquat            m_Rotation;
            float                   m_Radius;

            bool                    m_ShowAxis;
            bool                    m_ShowSphere;
            bool                    m_ShowBox;

            float                   m_AxisSize;

            float                   m_Width;
            float                   m_Length;
            float                   m_Height;

            std::array<std::string,  num_event_strings_v>   m_Strings;
            std::array<std::int32_t, num_event_ints_v>      m_Ints;
            std::array<float,        num_event_floats_v>    m_Floats;
            std::array<bool,         num_event_bools_v>     m_Bools;
            std::array<xcolori,      num_event_colors_v>    m_Colors;
        };
        
        struct prop_frame
        {
            xmath::fvec3            m_Scale;
            xmath::fquat            m_Rotation;
            xmath::fvec3            m_Translation;
            bool                    m_bVisible;
        };

        struct prop
        {
            std::string             m_Name;
            std::int32_t            m_iParentBone;
            std::string             m_Type;
        };

    public:
        
        void                    Serialize               ( bool                          isRead
                                                        , std::wstring_view             FileName
                                                        , xtextfile::file_type          Type      = xtextfile::file_type::BINARY
                                                        );
        void                    Save                    (std::wstring_view              FileName
                                                        ) const;
        void                    CleanUp                 ( void 
                                                        ) ;
        bool                    AreBonesFromSameBranch  ( const std::int32_t iBoneA
                                                        , const std::int32_t iBoneB 
                                                        ) const ;
        void                    PutBonesInLODOrder      ( void 
                                                        );
        void                    ComputeBonesL2W         ( std::span<xmath::fmat4>   Matrix
                                                        , float                     Frame 
                                                        ) const ;
        void                    ComputeBonesL2W         ( std::span<xmath::fmat4>   Matrix
                                                        , std::int32_t              iFrame
                                                        , bool                      bRemoveHorizMotion
                                                        , bool                      bRemoveVertMotion
                                                        , bool                      bRemoveYawMotion 
                                                        ) const  ;
        void                    ComputeBoneL2W          ( std::int32_t      iBone
                                                        , xmath::fmat4&     Matrix
                                                        , float             Frame 
                                                        ) const ;
        void                    ComputeRawBoneL2W       ( std::int32_t      iBone
                                                        , xmath::fmat4&     Matrix
                                                        , std::int32_t      iFrame 
                                                        ) const ;
        std::int32_t            GetBoneIDFromName       ( std::string_view   BoneName 
                                                        ) const;
        void                    ComputeBoneKeys         ( std::span<xmath::fquat>   Q
                                                        , std::span<xmath::fvec3>   S
                                                        , std::span<xmath::fvec3>   T
                                                        , float                     Frame 
                                                        ) const ;
        void                    BakeBindingIntoFrames   ( bool              BakeScale
                                                        , bool              BakeRotation
                                                        , bool              BakeTranslation 
                                                        ) ;
        void                    RemoveFramesFromRage    ( std::int32_t      StartingValidRange
                                                        , std::int32_t      EndingValidRange 
                                                        ) ;
        void                    DeleteBone              ( std::int32_t      iBone 
                                                        ) ;
        void                    DeleteBone              (std::string_view   BoneName
                                                        ) ;
        void                    DeleteDummyBones        ( void              // Deletes all bones with "dummy" in the name
                                                        ) ;
        bool                    ApplyNewSkeleton        ( const anim&       BindAnim 
                                                        ) ;
        bool                    HasSameSkeleton         ( const anim&       Anim 
                                                        ) const ;
        void                    setNewRoot              ( std::int32_t      Index 
                                                        ) ;
        void                    SanityCheck             ( void 
                                                        ) const ;
        bool                    isMaskedAnim            ( void 
                                                        ) const ;
        void                    CopyFrames              ( std::vector<key_frame>&   KeyFrame
                                                        , std::int32_t              iStart
                                                        , std::int32_t              nFrames 
                                                        ) const ;
        void                    InsertFrames            ( std::int32_t          iDestFrame
                                                        , std::span<key_frame>  KeyFrame 
                                                        ) ;
        void                    RencenterAnim           ( bool              TX
                                                        , bool              TY
                                                        , bool              TZ
                                                        , bool              Pitch
                                                        , bool              Yaw
                                                        , bool              Roll 
                                                        ) ;
        void                    CleanLoopingAnim        ( void 
                                                        ) ;
        anim&                   operator =              ( const anim&       Src 
                                                        );
    public:

        std::int32_t                    m_nFrames               {0};
        std::string                     m_Name                  {};
        std::int32_t                    m_FPS                   {60};

        std::vector<bone>               m_Bone                  {};
        std::vector<key_frame>          m_KeyFrame              {};                  // Bones in the Columns, key frames on the Rows
        std::vector<event>              m_Event                 {};
        std::vector<super_event>        m_SuperEvent            {};
        std::vector<prop>               m_Prop                  {};
        std::vector<prop_frame>         m_PropFrame             {};
    };

} // namespace xraw3d
