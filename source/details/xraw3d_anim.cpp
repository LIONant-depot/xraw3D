namespace xraw3d {

//--------------------------------------------------------------------------

bool anim::AreBonesFromSameBranch( std::int32_t iBoneA, std::int32_t iBoneB ) const
{
    std::int32_t iParent;

    // Check for finding B in A
    iParent =  m_Bone[ iBoneA ].m_iParent;// GetBoneIDFromName( m_Bone[ iBoneA ].m_ParentName );
    while( iParent != -1 )
    {
        if( iParent == iBoneB )
            return true ;

        iParent = m_Bone[iParent].m_iParent;// GetBoneIDFromName( m_Bone[iParent].m_ParentName );
    }

    // Check for finding A in B
    iParent = m_Bone[iBoneB].m_iParent;// GetBoneIDFromName( m_Bone[iBoneB].m_ParentName );
    while(iParent != -1)
    {
        if (iParent == iBoneA)
            return true ;

        iParent = m_Bone[iParent].m_iParent; // GetBoneIDFromName( m_Bone[iParent].m_ParentName );
    }

    return false ;
}

//--------------------------------------------------------------------------

anim& anim::operator =( const anim& Src )  
{ 
    m_nFrames       = Src.m_nFrames;
    m_FPS           = Src.m_FPS;
    m_Name          = Src.m_Name;

    m_Bone          = Src.m_Bone;
    m_KeyFrame      = Src.m_KeyFrame;
    m_Event         = Src.m_Event;
    m_SuperEvent    = Src.m_SuperEvent;
    m_Prop          = Src.m_Prop;
    m_PropFrame     = Src.m_PropFrame;

    return *this;
}

//--------------------------------------------------------------------------

struct temp_bone : public anim::bone
{
    std::int32_t    m_iBone;
    std::int32_t    m_iBoneRemap;
    std::int32_t    m_LODGroup;
    std::int32_t    m_Depth;
};

//--------------------------------------------------------------------------

// Gets bones in optimzal order ready for skeleton LOD
void anim::PutBonesInLODOrder( void )
{
    // Create temp bones
    std::vector<temp_bone> TempBone;
    TempBone.resize(m_Bone.size());

    //
    // Initialize structures
    //
    for ( temp_bone& TBone : TempBone )
    {
        const std::int32_t  Index   = std::int32_t(&TBone - &TempBone[0]);
        const bone&         Bone    = m_Bone[ Index ];

        *(bone*)&TBone      = Bone;
        TBone.m_iBoneRemap  = Index;
        TBone.m_iBone       = Index;

        //
        // Set the LOD group of the bone
        //
        auto iLodGroupStart = Bone.m_Name.find( "LOD[" );
        if ( iLodGroupStart != std::string::npos )
        {
            auto iLodGroupEnd = Bone.m_Name.find("]", iLodGroupStart+1 );
            if ( iLodGroupEnd == std::string::npos)
                throw( std::runtime_error( std::format( "ERROR: We found a bone[{}] with an LOD group but with a missing ']' ", Bone.m_Name.c_str() )));

            std::array<char,32> Buffer;
            std::int32_t  Length = std::int32_t(iLodGroupEnd - iLodGroupStart);
            assert( Length < sizeof(Buffer) );

            strncpy_s( Buffer.data(), Buffer.size(), &Bone.m_Name[ iLodGroupStart + 4 ], Length );

            TBone.m_LODGroup = std::atoi( Buffer.data() );
            assert( TBone.m_LODGroup >= 0 );
            assert( TBone.m_LODGroup <= 1000 );
        }
        else
        {
            if ( TBone.m_iParent == -1 ) 
                TBone.m_LODGroup = -2;
            else
                TBone.m_LODGroup = -1;
        }

        //
        // Setup depths
        //
        TBone.m_Depth = 0;
        for( std::int32_t i = TBone.m_iParent; i != -1; i = m_Bone[i].m_iParent )
        {
            assert( TBone.m_iParent != -1 );

            // Cascade the LOD group down if the user did not specify any new groups
            if ( TBone.m_LODGroup == -1 && TempBone[ TBone.m_iParent ].m_LODGroup >= 0 )
            {
                TBone.m_LODGroup = TempBone[TBone.m_iParent].m_LODGroup;
            }
            else if ( TempBone[ TBone.m_iParent ].m_LODGroup > TBone.m_LODGroup )
            {
                printf( "WARNING: You have specify a bone[%s] LOD group that is lower than the parent[%s], we will assign the same group as the parent.",
                        TBone.m_Name.c_str(),
                        TempBone[TBone.m_iParent].m_Name.c_str() );

                TBone.m_LODGroup = TempBone[TBone.m_iParent].m_LODGroup;
            }

            // Increment the depth
            ++TBone.m_Depth;
        }
    }

    //
    // Short bones properly
    //
    std::qsort( TempBone.data(), TempBone.size(), sizeof( temp_bone ), []( const void* pA, const void* pB ) -> std::int32_t
    {
        const temp_bone& A = *(const temp_bone*)pA;
        const temp_bone& B = *(const temp_bone*)pB;

        if ( A.m_LODGroup < B.m_LODGroup ) return -1;
        if ( A.m_LODGroup > B.m_LODGroup ) return  1;

        if ( A.m_iParent < B.m_iParent ) return -1;
        if ( A.m_iParent > B.m_iParent ) return  1;

        if ( A.m_iBone < B.m_iBone ) return -1;
        return ( A.m_iBone > B.m_iBone );
    } );

    // Setup remap bone indices
    for( std::int32_t i = 0 ; i < TempBone.size(); i++ )
        TempBone[ TempBone[i].m_iBone ].m_iBoneRemap = i ;

    // Remap parent indices
    for( std::int32_t i = 0 ; i < TempBone.size(); i++ )
    {
        std::int32_t iParent = TempBone[i].m_iParent ;
        if (iParent != -1)
            TempBone[i].m_iParent = TempBone[iParent].m_iBoneRemap ;
    }

    // Copy out the temp bones into the real bones
    for( std::int32_t i = 0 ; i < TempBone.size(); i++ )
    {
        m_Bone[i] = std::move(TempBone[i]);
    }

    // Validate
    for (std::int32_t i = 0 ; i < m_Bone.size() ; i++)
    {
        const auto& Bone = m_Bone[i];
        
        // Parent should always be above child!
        if( Bone.m_iParent >= i )
            throw( std::runtime_error("ERROR: Bone LOD Sort has failed!!! Make sure that LOD groups are setup correctly\n"));
    }

    // Remap all the keys
    std::vector<key_frame> NewKeys;
    NewKeys.resize(m_KeyFrame.size());

    const auto nBones = m_Bone.size();
    for ( auto iKey = 0u; iKey < m_KeyFrame.size(); iKey++ )
    {
        const auto iFrame    = iKey / nBones;
        const auto iBone     = iKey % m_Bone.size();
        const auto iOld      = (iFrame * nBones) + TempBone[ iBone ].m_iBone;
        
        NewKeys[ iKey ] = m_KeyFrame[ iOld ];
    }

    // set the new list
    m_KeyFrame = std::move(NewKeys);

    // Show hierarchy in debug window
    /*
    x_DebugMsg("\n\n\nLOD optimized bone order:\n") ;
    for( std::int32_t i = 0 ; i < m_Bone.size() ; i++)
    {
        std::int32_t iParent ;

        // Print indent
        iParent = m_Bone[i].m_iParent ;
        while(iParent != -1)
        {
            x_DebugMsg(" ") ;
            iParent = m_Bone[iParent].m_iParent ;
        }

        // Print name
        iParent = m_Bone[i].m_iParent ;
        xassert(iParent < i) ;
        if (iParent != -1)
            x_DebugMsg("%s (Parent=%s)\n", (const char*)m_Bone[i].m_Name, (const char*)m_Bone[iParent].m_Name) ;
        else
            x_DebugMsg("%s\n", (const char*)m_Bone[i].m_Name) ;
    }
    x_DebugMsg("\n\n\n") ;
    */
}

//--------------------------------------------------------------------------

void anim::Serialize
( bool                          isRead
, std::wstring_view             FileName
, xtextfile::file_type          FileType
)
{
    xtextfile::stream File;

    if( auto Err = File.Open(isRead, FileName, FileType ); Err )
        throw(std::runtime_error( std::string(Err.getMessage()) ));

    if( auto Err = File.Record
        ( "AnimInfo"
        , [&]( std::size_t, xerr& Err )
        {
               (Err = File.Field( "Name",    m_Name ))
            || (Err = File.Field( "FPS",     m_FPS ))
            || (Err = File.Field( "nFrames", m_nFrames ))
            ;
        })
      ; Err ) throw(std::runtime_error(std::string(Err.getMessage())));

    if( auto Err = File.Record
        ( "Skeleton"
        , [&]( std::size_t& C, xerr& Err )
        {
            if(isRead) m_Bone.resize(C);
            else       C   = m_Bone.size();
        }
        , [&](std::size_t I, xerr& Err )
        {
            int Index = int(I);
            if( Err = File.Field( "Index",        Index ) ) return;
            
            auto& Bone = m_Bone[Index];
               (Err = File.Field( "Name",         Bone.m_Name )                                                                                                 )
            || (Err = File.Field( "nChildren",    Bone.m_nChildren)                                                                                             )
            || (Err = File.Field( "iParent",      Bone.m_iParent)                                                                                               )
            || (Err = File.Field( "Scale",        Bone.m_BindScale.m_X,       Bone.m_BindScale.m_Y,       Bone.m_BindScale.m_Z)                                 )
            || (Err = File.Field( "Rotate",       Bone.m_BindRotation.m_X,    Bone.m_BindRotation.m_Y,    Bone.m_BindRotation.m_Z,    Bone.m_BindRotation.m_W ) )
            || (Err = File.Field( "Pos",          Bone.m_BindTranslation.m_X, Bone.m_BindTranslation.m_Y, Bone.m_BindTranslation.m_Z)                           )
            || (Err = File.Field( "bScaleKeys",   Bone.m_bScaleKeys)                                                                                            )
            || (Err = File.Field( "bRotKeys",     Bone.m_bRotationKeys)                                                                                         )
            || (Err = File.Field( "bPosKeys",     Bone.m_bTranslationKeys)                                                                                      )
            ;
        })
      ; Err ) throw(std::runtime_error( std::string(Err.getMessage())));


    if( auto Err = File.Record
        ( "KeyFrames"
        , [&](std::size_t& C, xerr& Err)
        {
            if (isRead) m_KeyFrame.resize(C);
            else        C   = m_KeyFrame.size();
        }
        , [&](std::size_t I, xerr& Err)
        {
            int Index = int(I);
            if (Err = File.Field("iKey", Index)) return;

            int iBone  = static_cast<int>(Index % m_Bone.size());
            if (Err = File.Field("iBone", iBone)) return;

            int iFrame = static_cast<int>(Index / m_Bone.size());
            if (Err = File.Field("iFrame", iFrame)) return;

            auto& Frame = m_KeyFrame[Index];
               (Err = File.Field("Scale",     Frame.m_Scale.m_X,       Frame.m_Scale.m_Y,       Frame.m_Scale.m_Z )                          )
            || (Err = File.Field("Rotate",    Frame.m_Rotation.m_X,    Frame.m_Rotation.m_Y,    Frame.m_Rotation.m_Z,       Frame.m_Rotation.m_W ) )
            || (Err = File.Field("Translate", Frame.m_Position.m_X,    Frame.m_Position.m_Y,    Frame.m_Position.m_Z )                      )
            ;
        })
      ; Err ) throw(std::runtime_error(std::string(Err.getMessage())));
}

//--------------------------------------------------------------------------

void anim::ComputeBonesL2W( std::span<xmath::fmat4> Matrix, float Frame ) const
{
    std::int32_t i;

    // Keep frame in range
    Frame = std::fmodf( Frame,float(m_nFrames-1) );

    // Compute integer and 
    std::int32_t iFrame0 = (std::int32_t)Frame;
    std::int32_t iFrame1 = (iFrame0+1)%m_nFrames;
    float fFrame  = Frame - iFrame0;

    // Loop through bones and build matrices
    const key_frame* pF0 = &m_KeyFrame[ iFrame0*m_Bone.size() ];
    const key_frame* pF1 = &m_KeyFrame[ iFrame1*m_Bone.size() ];
    for( i=0; i<m_Bone.size(); i++ )
    {
        xmath::fquat R = pF0->m_Rotation.Lerp(pF1->m_Rotation,  fFrame );
        xmath::fvec3 S = pF0->m_Scale.Lerp(pF1->m_Scale, fFrame );
        xmath::fvec3 T = pF0->m_Position.Lerp(pF1->m_Position, fFrame);
        pF0++;
        pF1++;
        Matrix[i].setupSRT( S, R, T );

        // Concatenate with parent
        if( m_Bone[i].m_iParent != -1 )
        {
            assert( m_Bone[i].m_iParent < i );
            Matrix[i] = Matrix[ m_Bone[i].m_iParent ] * Matrix[i];
        }
    }

    // Apply bind matrices
    for( i=0; i<m_Bone.size(); i++ )
    {
        Matrix[i] = Matrix[i] * m_Bone[i].m_BindMatrixInv;
    }
}

//--------------------------------------------------------------------------

void anim::ComputeBonesL2W( std::span<xmath::fmat4> Matrix
                          , std::int32_t            iFrame
                          , bool                    bRemoveHorizMotion
                          , bool                    bRemoveVertMotion
                          , bool                    bRemoveYawMotion ) const
{
    std::int32_t i;

    // Keep frame in range
    iFrame = iFrame % (m_nFrames-1) ;

    // Loop through bones and build matrices
    const key_frame* pF0 = &m_KeyFrame[ iFrame * m_Bone.size() ];
    for( i=0; i<m_Bone.size(); i++ )
    {
        // Root bone mayhem?
        if (i == 0)
        {
            // Lookup info
            xmath::fvec3    Scale = pF0->m_Scale ;
            xmath::fvec3    Trans = pF0->m_Position;
            xmath::fquat    Rot   = pF0->m_Rotation;

            // Remove horiz motion?
            if( bRemoveHorizMotion )
                Trans.m_X = Trans.m_Z = 0.0f ;

            // Remove vert motion?
            if( bRemoveVertMotion )
                Trans.m_Y = 0.0f ;

            // Remove yaw motion?
            if( bRemoveYawMotion )
            {
                // TO DO...
            }

            // Setup matrix from frame
            Matrix[i].setupSRT( Scale, Rot, Trans);
        }
        else
        {
            // Setup matrix from frame
            Matrix[i].setupSRT( pF0->m_Scale, pF0->m_Rotation, pF0->m_Position);
        }

        // Next frame
        pF0++;

        // Concatenate with parent
        if( m_Bone[i].m_iParent != -1 )
            Matrix[i] = Matrix[ m_Bone[i].m_iParent ] * Matrix[i];
    }

    // Apply bind matrices
    for( i=0; i<m_Bone.size(); i++ )
    {
        Matrix[i] = Matrix[i] * m_Bone[i].m_BindMatrixInv;
    }
}

//--------------------------------------------------------------------------

void anim::ComputeBoneL2W( std::int32_t iBone, xmath::fmat4& Matrix, float Frame ) const
{
    // Keep frame in range
    Frame = std::fmodf( Frame, float( m_nFrames - 1) );

    // Compute integer and 
    std::int32_t iFrame0 = (std::int32_t)Frame;
    std::int32_t iFrame1 = (iFrame0+1)%m_nFrames;
    float fFrame  = Frame - iFrame0;

    // Loop through bones and build matrices
    const key_frame* pF0 = &m_KeyFrame[ iFrame0 * m_Bone.size() ];
    const key_frame* pF1 = &m_KeyFrame[ iFrame1 * m_Bone.size() ];

    // Clear bone matrix
    Matrix.setupIdentity();

    // Run hierarchy from bone to root node
    std::int32_t I = iBone;
    while( I != -1 )
    {
        xmath::fquat R = pF0[I].m_Rotation.Lerp(pF1[I].m_Rotation, fFrame);
        xmath::fvec3 S = pF0[I].m_Scale.Lerp(pF1[I].m_Scale, fFrame);
        xmath::fvec3 T = pF0[I].m_Position.Lerp(pF1[I].m_Position, fFrame);

        xmath::fmat4 LM;
        LM.setupSRT(S, R, T);

        Matrix = LM * Matrix;
        I = m_Bone[I].m_iParent;
    }

    // Apply bind matrix
    Matrix = Matrix * m_Bone[iBone].m_BindMatrixInv;
}

//--------------------------------------------------------------------------

void anim::ComputeRawBoneL2W( std::int32_t iBone, xmath::fmat4& Matrix, std::int32_t iFrame ) const
{
    // Keep frame in range
    assert( (iFrame>=0) && (iFrame<m_nFrames) );

    // Loop through bones and build matrices
    const key_frame* pF = &m_KeyFrame[ iFrame * m_Bone.size() ];

    // Clear bone matrix
    Matrix.setupIdentity();

    // Run hierarchy from bone to root node
    std::int32_t I = iBone;
    while( I != -1 )
    {
        xmath::fquat R = pF[I].m_Rotation;
        xmath::fvec3 S = pF[I].m_Scale;
        xmath::fvec3 T = pF[I].m_Position;

        xmath::fmat4 LM;
        LM.setupSRT(S, R, T);

        Matrix = LM * Matrix;
        I = m_Bone[I].m_iParent;
    }

    // Apply bind matrix
    Matrix = Matrix * m_Bone[iBone].m_BindMatrixInv;
}

//--------------------------------------------------------------------------

void anim::ComputeBoneKeys( std::span<xmath::fquat> Q, std::span<xmath::fvec3> S, std::span<xmath::fvec3> T, float Frame ) const
{
    std::int32_t i;

    // Keep frame in range
    Frame = std::fmodf(Frame,(float)(m_nFrames-1));

    // Compute integer and 
    std::int32_t iFrame0 = (std::int32_t)Frame;
    std::int32_t iFrame1 = (iFrame0+1)%m_nFrames;
    float fFrame  = Frame - iFrame0;

    // Loop through bones and build matrices
    const key_frame* pF0 = &m_KeyFrame[ iFrame0 * m_Bone.size() ];
    const key_frame* pF1 = &m_KeyFrame[ iFrame1 * m_Bone.size() ];
    for( i=0; i<m_Bone.size(); i++ )
    {
        Q[i] = pF0->m_Rotation.Lerp(pF1->m_Rotation, fFrame);
        S[i] = pF0->m_Scale.Lerp(pF1->m_Scale, fFrame);
        T[i] = pF0->m_Position.Lerp(pF1->m_Position, fFrame);
        pF0++;
        pF1++;
    }
}

//--------------------------------------------------------------------------

std::int32_t anim::GetBoneIDFromName( std::string_view BoneName ) const
{
    std::int32_t i;
    for( i=0; i<m_Bone.size(); i++ )
    if( xstrtool::CompareI( BoneName, m_Bone[i].m_Name ) == 0 )
        return i;
    return -1;
}

//--------------------------------------------------------------------------

void anim::RemoveFramesFromRage( std::int32_t StartingValidRange, std::int32_t EndingValidRange )
{
    assert( StartingValidRange >= 0 );
    assert( EndingValidRange >= 0 );
    assert( EndingValidRange <= m_nFrames );
    assert( StartingValidRange <= m_nFrames );

    if( StartingValidRange == 0 && EndingValidRange == m_nFrames )
        return;

    std::vector<key_frame> NewRange;

    const std::int32_t nFrames = EndingValidRange - StartingValidRange;

    NewRange.resize(nFrames * m_Bone.size());

    for( std::int32_t i = StartingValidRange; i <EndingValidRange; i++ )
    {
        const std::int32_t iNewSpace = i - StartingValidRange;
        std::memcpy( &NewRange[ iNewSpace * m_Bone.size() ], &m_KeyFrame[ i * m_Bone.size()], sizeof(key_frame) * m_Bone.size() );
    }     

    // Set the new key frames
    m_KeyFrame = std::move(NewRange);
    m_nFrames  = nFrames;
}

//--------------------------------------------------------------------------

void anim::BakeBindingIntoFrames( bool DoScale, bool DoRotation, bool DoTranslation )
{
    std::int32_t i,j;

    //
    // Loop through frames of animation
    //
    std::vector<xmath::fmat4> L2W;
    L2W.resize(m_Bone.size());

    for( i=0; i<m_nFrames; i++ )
    {
        //
        // Compute matrices for current animation.
        // No binding is applied
        //
        for( j=0; j<m_Bone.size(); j++ )
        {
            key_frame* pF = &m_KeyFrame[ i*m_Bone.size() + j ];

            L2W[j].setupSRT( pF->m_Scale, pF->m_Rotation, pF->m_Position );

            // Concatenate with parent
            if( m_Bone[j].m_iParent != -1 )
            {
                L2W[j] = L2W[m_Bone[j].m_iParent] * L2W[j];
            }
        }

        //
        // Apply original bind matrices
        //
        for( j=0; j<m_Bone.size(); j++ )
        {
            L2W[j] = L2W[j] * m_Bone[j].m_BindMatrixInv;
        }

        //
        // Remove bind translation and scale matrices
        //
        for( j=0; j<m_Bone.size(); j++ )
        {
            xmath::fquat R = m_Bone[j].m_BindRotation;
            xmath::fvec3 S = m_Bone[j].m_BindScale;
            xmath::fvec3 T = m_Bone[j].m_BindTranslation;

            if( DoScale )       S.setup(1);
            if( DoRotation )    R.setupIdentity();
            if( DoTranslation ) T.setup(0);

            xmath::fmat4 BindMatrix;
            BindMatrix.setupSRT( S, R, T);
            L2W[j] = L2W[j] * BindMatrix;
        }

        // Convert back to local space transform
        for( j = static_cast<int>(m_Bone.size()-1); j>0; j-- )
            if( m_Bone[j].m_iParent != -1 )
            {
                auto PM = L2W[ m_Bone[j].m_iParent ];
                PM.InverseSRT();
                L2W[j] = PM * L2W[j];
            }

        // Pull out rotation scale and translation
        for( j=0; j<m_Bone.size(); j++ )
        {
            key_frame* pF       = &m_KeyFrame[i * m_Bone.size() + j ];
            
            pF->m_Scale         = L2W[j].ExtractScale();
            pF->m_Rotation      = L2W[j];
            pF->m_Position      = L2W[j].ExtractPosition();
        }
    }

    // Remove wanted attributes out of the binding
    for( i=0; i<m_Bone.size(); i++ )
    {
        if ( DoTranslation )
            m_Bone[ i ].m_BindTranslation.setup(0);

        if( DoScale ) 
            m_Bone[i].m_BindScale.setup(1);

        if( DoRotation )
            m_Bone[i].m_BindRotation.setupIdentity();

        m_Bone[i].m_BindMatrix.setupSRT( m_Bone[i].m_BindScale, m_Bone[i].m_BindRotation, m_Bone[i].m_BindTranslation);
        m_Bone[i].m_BindMatrixInv = m_Bone[i].m_BindMatrix;
        m_Bone[i].m_BindMatrixInv.InverseSRT();
    }
}

//--------------------------------------------------------------------------

void anim::DeleteDummyBones( void )
{
    std::int32_t iBone = 0;
    while( iBone < m_Bone.size() )
    {
        if(m_Bone[iBone].m_Name.find( "dummy") != std::string::npos)
        {
            //Check if it is the root.  If it is, make sure it is not the only root; that is,
            // we can only delete a root bone if it only has one child (because then its child
            // can become the new root)
            if(m_Bone[iBone].m_iParent == -1)
            {
                std::int32_t nChildren = 0;
                for(std::size_t count = 0; count < m_Bone.size(); count++)
                {
                    if( m_Bone[count].m_iParent == iBone )
                        nChildren++;
                }

                if(nChildren == 1)
                {
                    //x_DebugMsg("Bone is root, but can be removed: '%s'\n", m_pBone[iBone].Name);                
                    DeleteBone(iBone);
                    iBone = 0;
                }
                else
                {
                    //x_DebugMsg("Bone is sole remaining root: '%s'\n", m_pBone[iBone].Name);
                    iBone++;
                }
            }
            else
            {
                DeleteBone(iBone);
                iBone = 0;
            }
        }
        else
        {
            iBone++;
        }
    }

/*
    for(iBone = 0; iBone < m_nBones; iBone++)
    {
        x_DebugMsg("Bone Index: %3d Parent: %3d Name: '%s'\n", iBone, m_pBone[iBone].iParent, m_pBone[iBone].Name);
    }
*/
}
    
//--------------------------------------------------------------------------

void anim::DeleteBone( std::string_view BoneName )
{
    std::int32_t iBone = this->GetBoneIDFromName(BoneName);
    if(iBone != -1)
        DeleteBone(iBone);
    return;
}

//--------------------------------------------------------------------------

void anim::DeleteBone( std::int32_t iBone )
{
    //x_DebugMsg("Deleting bone: '%s'\n", m_pBone[iBone].Name);
    std::int32_t i,j;
    //xassertS( m_Bone.size() > 1, TempDebugFileName );

    if (m_Bone.empty()) return;

    //
    // Allocate new bones and frames
    //
    std::int32_t               nNewBones = static_cast<std::int32_t>(m_Bone.size()-1);
    std::vector<bone>          NewBone;
    std::vector<key_frame>     NewFrame;

    NewBone.resize(nNewBones);
    NewFrame.resize(nNewBones * m_nFrames);
    
    //
    // Check and see if bone has any children
    //
    bool HasChildren = false;
    for( i=0; i<m_Bone.size(); i++ )
        if( m_Bone[i].m_iParent == iBone )
            HasChildren = true;

    //
    // Build new hierarchy
    //
    {
        // Copy over remaining bones
        j=0;
        for( i=0; i<m_Bone.size(); i++ )
            if( i != iBone )
            {
                NewBone[j] = m_Bone[i];
                j++;
            }

        // Patch children of bone
        for( i=0; i<nNewBones; i++ )
            if( NewBone[i].m_iParent == iBone )
            {
                NewBone[i].m_iParent = m_Bone[iBone].m_iParent;
            }

        // Patch references to any bone > iBone
        for( i=0; i<nNewBones; i++ )
            if( NewBone[i].m_iParent > iBone )
            {
                NewBone[i].m_iParent--;
            }
    }


    // 
    // If there were no children then we can quickly copy over the keys
    //
    if( !HasChildren )
    {
        //
        // Loop through frames of animation
        //
        std::int32_t k=0;
        for( i=0; i<m_nFrames; i++ )
        for( j=0; j<m_Bone.size(); j++ )
        {
            if( j!=iBone )
                NewFrame[k++] = m_KeyFrame[ i*m_Bone.size() + j ];
        }
    }
    else
    {
        //
        // Loop through frames of animation
        //
        std::vector<xmath::fmat4> L2W;
        L2W.resize(m_Bone.size());

        for( i=0; i<m_nFrames; i++ )
        {
            // Compute matrices for current animation.
            for( j=0; j<m_Bone.size(); j++ )
            {
                const key_frame* pF = &m_KeyFrame[ i*m_Bone.size()+j ];

                L2W[j].setupSRT( pF->m_Scale, pF->m_Rotation, pF->m_Position);

                // Concatenate with parent
                if( m_Bone[j].m_iParent != -1 )
                {
                    L2W[j] = L2W[ m_Bone[j].m_iParent ] * L2W[j];
                }
            }

            // Apply original bind matrices
            //for( j=0; j<m_Bone.size(); j++ )
            //{
            //    L2W[j] = L2W[j] * m_Bone[j].m_BindMatrixInv;
            //}

            // Shift bones down to align with NewBones
            if( iBone != (m_Bone.size()-1) ) 
                std::memmove( &L2W[iBone], &L2W[ iBone+1 ], sizeof(xmath::fmat4)*(m_Bone.size()-iBone-1) );

            //for( j=iBone+1; j<m_Bone.size(); j++ )
            //    L2W[j-1] = L2W[j];


            // Remove bind translation and scale matrices
            //for( j=0; j<nNewBones; j++ )
            //{
            //    L2W[j] = L2W[j] * NewBone[j].m_BindMatrix;
            //}

            // Convert back to local space transform
            for( j=nNewBones-1; j>0; j-- )
                if( NewBone[j].m_iParent != -1 )
                {
                    xmath::fmat4 PM = L2W[ NewBone[j].m_iParent ];
                    PM.InverseSRT();
                    L2W[j] = PM * L2W[j];
                }

            // Pull out rotation scale and translation
            for( j=0; j<nNewBones; j++ )
            {
                key_frame* pF       = &NewFrame[i*nNewBones + j];
                
                pF->m_Scale     = L2W[j].ExtractScale();
                pF->m_Rotation  = L2W[j];
                pF->m_Position  = L2W[j].ExtractPosition();
            }
        }
    }

    // free current allocations
    m_Bone.clear();
    m_KeyFrame.clear();

    m_Bone     = std::move(NewBone);
    m_KeyFrame = std::move(NewFrame);
}

//--------------------------------------------------------------------------

bool anim::ApplyNewSkeleton( const anim& BindAnim )
{
    std::int32_t i,j,k;
    bool Problem=false;

    //
    // Handle setting the new root
    //
    {
        assert( -1 == BindAnim.m_Bone[0].m_iParent );
        std::int32_t iRoot = GetBoneIDFromName( BindAnim.m_Bone[0].m_Name );
        if( iRoot == -1 )
            throw(std::runtime_error("ERROR: Failt to apply the new skeleton because I was not able to find the rootbone in the old skeleton"));

        // Set the new root first then we can deal with more complex bone removal stuff
        if( iRoot != 0 )
            setNewRoot( iRoot );
    }

    //
    // Remove all bones not in BindAnim
    //
    i = 0;
    while( i < m_Bone.size() )
    {
        for( j=0; j<BindAnim.m_Bone.size(); j++ )
        if( xstrtool::CompareI( m_Bone[i].m_Name, BindAnim.m_Bone[j].m_Name ) == 0 )
            break;

        if( j == BindAnim.m_Bone.size() )
        {
            if( m_Bone.size() == 1 )
            {
                throw(std::runtime_error( "ERROR: has no bones in bind." ));
            }

            DeleteBone( i );
            i=-1;
        }

        i++;
    }


    //
    // Allocate new bones and frames
    //
    std::vector<bone>        NewBone;
    std::vector<key_frame>   NewFrame;

    NewBone.resize(BindAnim.m_Bone.size());
    NewFrame.resize(BindAnim.m_Bone.size() * m_nFrames);

    //
    // Copy over bind skeleton
    //
    std::memcpy( NewBone.data(), BindAnim.m_Bone.data(), sizeof(bone)*BindAnim.m_Bone.size() );

    //
    // Construct frames
    //
    for( i=0; i<BindAnim.m_Bone.size(); i++ )
    {
        // Lookup bone in current anim
        for( j=0; j<m_Bone.size(); j++ )
            if( xstrtool::CompareI( m_Bone[j].m_Name, NewBone[i].m_Name ) == 0 )
                break;

        // Check if bone is present
        if( j == m_Bone.size() )
        {
            Problem = true;

            // No bone present.  
            // Copy over first frame of BindAnim

            key_frame Key;
            Key.m_Rotation  = BindAnim.m_Bone[i].m_BindRotation;
            Key.m_Scale     = BindAnim.m_Bone[ i ].m_BindScale;
            Key.m_Position.setup(0);

            for( k=0; k<m_nFrames; k++ )
                NewFrame[k*BindAnim.m_Bone.size() + i] = Key; //.identity();// = BindAnim.m_KeyFrame[i];
        }
        else
        {
            // Copy IsLayer over to new bones
            NewBone[i].m_bIsMasked = m_Bone[j].m_bIsMasked;

            // Copy data into new bone slot
            for( k=0; k<m_nFrames; k++ )
                NewFrame[k*BindAnim.m_Bone.size() + i] = m_KeyFrame[k*m_Bone.size()+j];
        }
    }

    //
    // Free current allocations and provide new ones
    //
    m_Bone.clear();
    m_KeyFrame.clear();
    
    m_Bone      = std::move(NewBone);
    m_KeyFrame  = std::move(NewFrame);

    return !Problem;
}

//--------------------------------------------------------------------------

bool anim::HasSameSkeleton( const anim& Anim ) const
{
    std::int32_t i;

    if( m_Bone.size() != Anim.m_Bone.size() )
        return false;

    for( i=0; i<m_Bone.size(); i++ )
    {
        const bone& B0 = m_Bone[i];
        const bone& B1 = Anim.m_Bone[i];

        if( xstrtool::CompareI( B0.m_Name, B1.m_Name ) != 0 )
            return false;

        if( B0.m_iParent != B1.m_iParent )
            return false;

        if( B0.m_nChildren != B1.m_nChildren )
            return false;

        for( std::int32_t j=0; j<4*4; j++ )
        {
            float D = std::abs( ((float*)&B0)[j] - ((float*)&B1)[j] );
            if( D > 0.0001f )
                return false;
        }
    }

    return true;
}

//--------------------------------------------------------------------------

void anim::SanityCheck( void ) const
{
    assert( (m_Bone.size()>0) && (m_Bone.size()<2048) );
    assert( (m_nFrames>=0) && (m_nFrames<65536) );
}

//--------------------------------------------------------------------------

bool anim::isMaskedAnim( void ) const
{
    for( std::int32_t i=0; i<m_Bone.size(); i++ )
        if( m_Bone[i].m_bIsMasked )
            return true;

    return false;
}

//--------------------------------------------------------------------------

void anim::CleanUp( void )
{
    PutBonesInLODOrder();
    DeleteDummyBones();
}

//--------------------------------------------------------------------------

void anim::setNewRoot( std::int32_t Index )
{

    //
    // Allocate new bones and frames
    //
    const std::int32_t          nParentsBones   = Index;
    const std::int32_t          nNewBones       = static_cast<std::int32_t>(m_Bone.size() - nParentsBones);
    std::vector<bone>           NewBone;
    std::vector<key_frame>      NewFrame;

    NewBone.resize(nNewBones);
    NewBone.resize(nNewBones * m_nFrames);

    //
    // Build new hierarchy
    //
    {
        // Copy over remaining bones
        for( std::int32_t i=0; i<nNewBones; i++ )
            NewBone[i] = m_Bone[ Index + i ];

        // Patch children of bone
        for ( std::int32_t i = 1; i < nNewBones; i++ )
            if ( NewBone[ i ].m_iParent < Index )
                throw(std::runtime_error("ERROR: While setting the new root bone I found children accessing its parents"));
          
        // Patch references to any bone > Index
        NewBone[ 0 ].m_iParent = -1;
        for ( std::int32_t i = 1; i<nNewBones; i++ )
        {
            NewBone[ i ].m_iParent -= nParentsBones;
            assert( NewBone[ i ].m_iParent >=0 );
        }
    }

    //
    // Loop through frames of animation
    //
    std::vector<xmath::fmat4> L2W;
    L2W.resize(m_Bone.size());

    for ( std::int32_t iFrame = 0; iFrame < m_nFrames; iFrame++ )
    {
        // Compute matrices for current animation.
        for ( std::int32_t j = 0; j < m_Bone.size(); j++ )
        {
            const key_frame& F = m_KeyFrame[ iFrame*m_Bone.size() + j ];

            L2W[ j ].setupSRT(F.m_Scale, F.m_Rotation, F.m_Position);

            // Concatenate with parent
            if ( m_Bone[ j ].m_iParent != -1 )
            {
                L2W[ j ] = L2W[ m_Bone[ j ].m_iParent ] * L2W[ j ];
            }
        }

        // Shift bones down to align with NewBones
        std::memmove( &L2W[0], &L2W[ Index ], sizeof( xmath::fmat4)*nNewBones );

        // Convert back to local space transform
        for ( std::int32_t j = nNewBones - 1; j>0; j-- )
        {
            assert( NewBone[ j ].m_iParent != -1 );
            xmath::fmat4 PM = L2W[ NewBone[ j ].m_iParent ];
            PM.InverseSRT();
            L2W[ j ] = PM * L2W[ j ];
        }

        // Pull out rotation scale and translation
        for (std::int32_t  j = 0; j < nNewBones; j++ )
        {
            key_frame& Frame = NewFrame[ iFrame*nNewBones + j ];

            Frame.m_Scale       = L2W[ j ].ExtractScale();
            Frame.m_Rotation    = L2W[ j ];
            Frame.m_Position    = L2W[ j ].ExtractPosition();
        }
    }

    // set the new data
    m_Bone      = std::move(NewBone);
    m_KeyFrame  = std::move(NewFrame);
}


//--------------------------------------------------------------------------

void anim::CopyFrames( std::vector<key_frame>& KeyFrame, std::int32_t iStart, std::int32_t nFrames ) const
{
    assert( iStart >= 0 );
    assert( nFrames > 0 );
    assert( iStart + nFrames <= m_nFrames );

    const std::int32_t nBones = static_cast<std::int32_t>(m_Bone.size());
    KeyFrame.resize(nBones * nFrames);
    std::memcpy( &KeyFrame[0], &m_KeyFrame[ iStart * nBones ], sizeof(key_frame)*nBones*nFrames );
}

//--------------------------------------------------------------------------

void anim::InsertFrames( std::int32_t iDestFrame, std::span<key_frame> KeyFrame )
{
    assert( iDestFrame >= 0 );
    if( KeyFrame.size() == 0 )
        return;

    assert( KeyFrame.size() > 0 );
    assert( iDestFrame <= m_nFrames );

    const std::int32_t nBones        = static_cast<std::int32_t>(m_Bone.size());
    const std::int32_t oldnFrames    = m_nFrames;

    // Update the number of frames in the anim
    m_nFrames += static_cast<std::int32_t>(KeyFrame.size()/nBones);

    if( m_KeyFrame.size() == 0 )
    {
        m_KeyFrame.assign( KeyFrame.begin(), KeyFrame.end() );
        return;
    }

    m_KeyFrame.resize(m_KeyFrame.size() + KeyFrame.size());
    
    if( iDestFrame != oldnFrames )
    {
        std::memmove( &m_KeyFrame[ iDestFrame * nBones + KeyFrame.size() ], 
                   &m_KeyFrame[ iDestFrame * nBones ], 
                   sizeof(key_frame)*KeyFrame.size() );
    }

    std::memcpy( &m_KeyFrame[ iDestFrame * nBones ], &KeyFrame[0], sizeof(key_frame)*KeyFrame.size() );
}

//--------------------------------------------------------------------------

void anim::RencenterAnim( bool TX, bool TY, bool TZ, bool Pitch, bool Yaw, bool Roll )
{
    if( TX | TY | TZ )
    {
        const std::int32_t       nBones          =   static_cast<std::int32_t>(m_Bone.size());
        const xmath::fvec3       LinearVelocity  =   m_KeyFrame[ nBones * 1 ].m_Position - m_KeyFrame[ nBones * 0 ].m_Position;
        const xmath::fvec3       CurrentCenter   =   m_KeyFrame[ nBones * 0 ].m_Position;
        xmath::fvec3             NewCenterDelta(0);

        if( TX )
            NewCenterDelta.m_X = LinearVelocity.m_X - CurrentCenter.m_X;

        if ( TY )
            NewCenterDelta.m_Y = LinearVelocity.m_Y - CurrentCenter.m_Y;

        if ( TZ )
            NewCenterDelta.m_Z = LinearVelocity.m_Z - CurrentCenter.m_Z;

        for( std::int32_t i=0; i<m_nFrames; i++ )
        {
            auto& Frame = m_KeyFrame[ nBones * i ]; 
            Frame.m_Position += NewCenterDelta; 
        }
    }

    //
    // Re-aligned Rotation
    //
    if( Pitch | Yaw | Roll )
    {
        const std::int32_t  nBones          = static_cast<std::int32_t>(m_Bone.size());
        xmath::radian3      InvRotation     = xmath::fquat( m_KeyFrame[ 0 ].m_Rotation ).Inverse().ToEuler();

        if( !Pitch ) InvRotation.m_Pitch = 0_xdeg;
        if( !Yaw )   InvRotation.m_Yaw   = 0_xdeg;
        if( !Roll )  InvRotation.m_Roll  = 0_xdeg;

        const xmath::fquat InvRotFiltered( InvRotation );

        for( std::int32_t i=0; i<m_nFrames; i++ )
        {
            auto& Frame = m_KeyFrame[ nBones * i ]; 
            Frame.m_Rotation = InvRotFiltered * Frame.m_Rotation;
            Frame.m_Position = InvRotFiltered * Frame.m_Position;
        }
    }
}

//--------------------------------------------------------------------------

void anim::CleanLoopingAnim( void )
{
    const std::int32_t                      nFrames         = m_nFrames;
    const std::int32_t                      nBones          = static_cast<std::int32_t>(m_Bone.size());
    const std::int32_t                      nAffectedFrames = m_FPS / 19;
    std::vector<xmath::transform3>          ContinuesFrames;
    std::vector<xmath::transform3>          NewFrames;

    //
    // Create the array of the new frames and fill it with information
    //
    NewFrames.resize(nAffectedFrames * 2 * nBones);
    ContinuesFrames.resize(nAffectedFrames * 2 * nBones);

    //
    // Create the frames in the same loop space
    //
    {
        const auto&             RootLastFrame   = m_KeyFrame[ (nFrames-1) * nBones ];
        //const auto&           RootFirstFrame  = m_KeyFrame[ 0 ];
        const xmath::radian     DeltaYaw        = RootLastFrame.m_Rotation.ToEuler().m_Yaw;
        const xmath::fquat      DeltaRot        = xmath::fquat( xmath::radian3(0_xdeg,DeltaYaw,0_xdeg) );

        for( std::int32_t i=0; i<nAffectedFrames*2; i++ )
        {
            const std::int32_t  iFrame    = ( nFrames - (nAffectedFrames - i) ) % nFrames;
            auto&               NewFrame  =  ContinuesFrames[ i * nBones ];
            const auto&         Frame     =  m_KeyFrame[iFrame * nBones ];

            // Copy one frame worth of data
            std::memcpy( &NewFrame, &Frame, sizeof(xmath::transform3) * nBones );

            // Set the base frames to be in the space of the space of the last frame loop
            if( iFrame < nAffectedFrames )
            {
                NewFrame.m_Rotation    = DeltaRot * Frame.m_Rotation;

                const xmath::fvec3 RelTrans = DeltaRot * Frame.m_Position;

                NewFrame.m_Position = RootLastFrame.m_Position + RelTrans;
                NewFrame.m_Position.m_Y = Frame.m_Position.m_Y;
            }
        }
    }

    //
    // Interpolate Frames
    //
    {
        const std::int32_t iFrame0 = 0;
        const std::int32_t iFrame1 = (nAffectedFrames*2)-1;
        for( std::int32_t i=0; i<nAffectedFrames*2; i++ )
        {
            float T = (i + 0.5f)/(float)(nAffectedFrames*2);

            // Blend Frames
            for( std::int32_t b=0;b<nBones;b++)
            {
                const auto      KeyFrame0    =  ContinuesFrames[ iFrame0 * nBones + b ];
                const auto      KeyFrame1    =  ContinuesFrames[ iFrame1 * nBones + b ];
                auto&           KeyFrameDest =  NewFrames[ i       * nBones + b ];
            
                KeyFrameDest = xmath::transform3::fromBlend(KeyFrame0, KeyFrame1, T);
            }
        }
    }

    //
    // New Loops interpolates between the new computed frames and the blends
    //
    for( std::int32_t j=0; j<nAffectedFrames; j++ )
    {
        const std::int32_t nNewAffectedFrames = nAffectedFrames-j;
        const std::int32_t iFrame0 = j;
        const std::int32_t iFrame1 = (nNewAffectedFrames*2)-1;
    
        for( std::int32_t i=0; i<nNewAffectedFrames*2; i++ )
        {
            float T = (i + 0.5f)/(float)(nNewAffectedFrames*2);

            // Blend Frames
            for( std::int32_t b=0;b<nBones;b++)
            {
                const auto&         KeyFrame0     =  ContinuesFrames[ iFrame0 * nBones + b ];
                const auto&         KeyFrame1     =  ContinuesFrames[ iFrame1 * nBones + b ];
                xmath::transform3   KeyFrameDest;

                KeyFrameDest = xmath::transform3::fromBlend( KeyFrame0, KeyFrame1, T);
                
                auto&            KeyFrameFinal = NewFrames[ (j+i) * nBones + b ];

                KeyFrameFinal.Blend( KeyFrameDest, 0.5 );
            }
        }
    }

    //
    // Save back the frames
    //
    {
        const auto              LastNewFrame  = NewFrames[ (nAffectedFrames-1) * nBones ];
        const auto              FirstNewFrame = NewFrames[ nAffectedFrames * nBones ];  
        const auto              DeltaYaw      = LastNewFrame.m_Rotation.ToEuler().m_Yaw; 
        const xmath::fquat      DeltaRot      = xmath::fquat( xmath::radian3(0_xdeg,-DeltaYaw,0_xdeg) );

        for( std::int32_t i=0; i<nAffectedFrames*2; i++ )
        {
            const std::int32_t  iFrame      = ( nFrames - (nAffectedFrames - i) ) % nFrames;
            auto&               NewFrame    =  NewFrames[ i * nBones ];
            auto&               Frame       =  m_KeyFrame[iFrame * nBones ];

            if( iFrame < nAffectedFrames )
            {
                NewFrame.m_Rotation  = DeltaRot * NewFrame.m_Rotation;

                xmath::fvec3 Trans    = NewFrame.m_Position - LastNewFrame.m_Position;
                Trans.m_Y = NewFrame.m_Position.m_Y;

                NewFrame.m_Position = DeltaRot * Trans;
            }

            // Copy one frame worth of data
            std::memcpy( &Frame, &NewFrame, sizeof(xmath::transform3) * nBones );
        }
    }
}


} // namespace xraw3d