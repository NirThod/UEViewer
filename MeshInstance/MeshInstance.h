#ifndef __MESHINSTANCE_H__
#define __MESHINSTANCE_H__

/*-----------------------------------------------------------------------------
	Basic CMeshInstance class
-----------------------------------------------------------------------------*/

inline void SetAxis(const FRotator &Rot, CAxis &Axis)
{
	CVec3 angles;
	//?? check: swapped pitch and roll ?
	angles[YAW]   = -Rot.Yaw   / 32768.0f * 180;
	angles[ROLL]  = -Rot.Pitch / 32768.0f * 180;
	angles[PITCH] = -Rot.Roll  / 32768.0f * 180;
	Axis.FromEuler(angles);
}


class CMeshInstance
{
public:
	// common properties
	CCoords			BaseTransform;			// rotation for mesh; have identity axis
	CCoords			BaseTransformScaled;	// rotation for mesh with scaled axis
	// debugging
	bool			bShowNormals;
	bool			bColorMaterials;
	bool			bWireframe;

	CMeshInstance()
	:	bShowNormals(false)
	,	bColorMaterials(false)
	,	bWireframe(false)
	{}

	virtual ~CMeshInstance()
	{}

	void SetMaterial(UMaterial *Mat, int Index, int PolyFlags)
	{
		guard(CMeshInstance::SetMaterial);
		if (!bColorMaterials)
		{
			if (Mat)
				Mat->Bind(PolyFlags);
			else
				BindDefaultMaterial();
		}
		else
		{
			BindDefaultMaterial(true);
			glDisable(GL_CULL_FACE);
			int color = Index + 1;
			if (color > 7) color = 7;
#define C(n)	( ((color >> n) & 1) * 0.5f + 0.3f )
			glColor3f(C(0), C(1), C(2));
#undef C
		}
		unguard;
	}

	virtual void Draw() = 0;
};


class CLodMeshInstance : public CMeshInstance
{
public:
	// linked data
	const ULodMesh	*pMesh;

	CLodMeshInstance()
	:	pMesh(NULL)
	{}

	virtual void SetMesh(const ULodMesh *Mesh)
	{
		pMesh = Mesh;
		SetAxis(Mesh->RotOrigin, BaseTransform.axis);
		BaseTransform.origin[0] = Mesh->MeshOrigin.X * Mesh->MeshScale.X;
		BaseTransform.origin[1] = Mesh->MeshOrigin.Y * Mesh->MeshScale.Y;
		BaseTransform.origin[2] = Mesh->MeshOrigin.Z * Mesh->MeshScale.Z;

		BaseTransformScaled.axis = BaseTransform.axis;
		CVec3 tmp;
		tmp[0] = 1.0f / Mesh->MeshScale.X;
		tmp[1] = 1.0f / Mesh->MeshScale.Y;
		tmp[2] = 1.0f / Mesh->MeshScale.Z;
		BaseTransformScaled.axis.PrescaleSource(tmp);
		BaseTransformScaled.origin = (CVec3&)Mesh->MeshOrigin;
	}

	void SetMaterial(int Index)
	{
		guard(CLodMeshInstance::SetMaterial);
		int TexIndex = 1000000, PolyFlags = 0;
		if (Index < pMesh->Materials.Num())
		{
			const FMeshMaterial &M = pMesh->Materials[Index];
			TexIndex  = M.TextureIndex;
			PolyFlags = M.PolyFlags;
		}
		// it is possible, that Textures array is empty (mesh textured by script)
		UMaterial *Mat = (TexIndex < pMesh->Textures.Num()) ? pMesh->Textures[TexIndex] : NULL;
		CMeshInstance::SetMaterial(Mat, Index, PolyFlags);
		unguard;
	}

	virtual void UpdateAnimation(float TimeDelta) = 0;

	// animation control
	void PlayAnim(const char *AnimName, float Rate = 1, float TweenTime = 0, int Channel = 0)
	{
		PlayAnimInternal(AnimName, Rate, TweenTime, Channel, false);
	}
	void LoopAnim(const char *AnimName, float Rate = 1, float TweenTime = 0, int Channel = 0)
	{
		PlayAnimInternal(AnimName, Rate, TweenTime, Channel, true);
	}
	void TweenAnim(const char *AnimName, float TweenTime, int Channel = 0)
	{
		PlayAnimInternal(AnimName, 0, TweenTime, Channel, false);
	}

	virtual void AnimStopLooping(int Channel = 0) = 0;
	virtual void FreezeAnimAt(float Time, int Channel = 0) = 0;

	// animation state
	virtual void GetAnimParams(int Channel, const char *&AnimName,
		float &Frame, float &NumFrames, float &Rate) const = 0;
	virtual bool IsTweening(int Channel = 0) = 0;

	// animation enumeration
	virtual int GetAnimCount() const = 0;
	virtual const char *GetAnimName(int Index) const = 0;

protected:
	virtual void PlayAnimInternal(const char *AnimName, float Rate, float TweenTime, int Channel, bool Looped) = 0;
};


/*-----------------------------------------------------------------------------
	CVertMeshInstance class
-----------------------------------------------------------------------------*/

class CVertMeshInstance : public CLodMeshInstance
{
public:
	CVertMeshInstance()
	:	AnimIndex(-1)
	,	AnimTime(0)
	{}
	virtual void Draw();

	// animation control
	virtual void AnimStopLooping(int Channel)
	{
		AnimLooped = false;
	}
	virtual void FreezeAnimAt(float Time, int Channel);

	// animation state
	virtual void GetAnimParams(int Channel, const char *&AnimName,
		float &Frame, float &NumFrames, float &Rate) const;
	virtual bool IsTweening(int Channel)
	{
		return false;
	}

	// animation enumeration
	virtual int GetAnimCount() const
	{
		const UVertMesh *Mesh = GetMesh();
		return Mesh->AnimSeqs.Num();
	}
	virtual const char *GetAnimName(int Index) const
	{
		guard(CVertMeshInstance::GetAnimName);
		if (Index < 0) return "None";
		return GetMesh()->AnimSeqs[Index].Name;
		unguard;
	}

	virtual void UpdateAnimation(float TimeDelta);

protected:
	// animation state
	int			AnimIndex;			// current animation sequence
	float		AnimTime;			// current animation frame
	float		AnimRate;			// animation rate, frames per seconds
	bool		AnimLooped;

	const UVertMesh *GetMesh() const
	{
		return static_cast<const UVertMesh*>(pMesh);
	}
	int FindAnim(const char *AnimName) const;
	virtual void PlayAnimInternal(const char *AnimName, float Rate, float TweenTime, int Channel, bool Looped);
};


/*-----------------------------------------------------------------------------
	CSkelMeshInstance class
-----------------------------------------------------------------------------*/

#define MAX_SKELANIMCHANNELS	32

struct FVertInfluences;

class CSkelMeshInstance : public CLodMeshInstance
{
	struct CAnimChan
	{
		int			AnimIndex1;		// current animation sequence; -1 for default pose
		int			AnimIndex2;		// secondary animation for blending with; -1 = not used
		float		Time;			// current animation frame for primary animation
		float		SecondaryBlend;	// value = 0 (use primary animation) .. 1 (use secondary animation)
		float		BlendAlpha;		// blend with previous channels; 0 = not affected, 1 = fully affected
		int			RootBone;		// root animation bone
		float		Rate;			// animation rate multiplier for Anim.Rate
		float		TweenTime;		// time to stop tweening; 0 when no tweening at all
		float		TweenStep;		// fraction between current pose and desired pose; updated in UpdateAnimation()
		bool		Looped;
	};

public:
	// mesh state
	int			LodNum;
	// debugging
	int			ShowSkel;			// 0 - mesh, 1 - mesh+skel, 2 - skel only
	bool		ShowInfluences;
	bool		ShowLabels;
	bool		ShowAttach;

	CSkelMeshInstance()
	:	LodNum(-1)
	,	LastLodNum(-2)				// differs from LodNum and from all other values
	,	MaxAnimChannel(-1)
	,	BoneData(NULL)
	,	Wedges(NULL)
	,	Skinned(NULL)
	,	InfColors(NULL)
	,	ShowSkel(0)
	,	ShowLabels(false)
	,	ShowAttach(false)
	,	ShowInfluences(false)
	{
		ClearSkelAnims();
	}

	virtual void SetMesh(const ULodMesh *Mesh);
	virtual ~CSkelMeshInstance();
	void ClearSkelAnims();
//??	void StopAnimating(bool ClearAllButBase);
	virtual void Draw();

	void DumpBones();
	void DrawSkeleton(bool ShowLabels);
	void DrawAttachments();
	void DrawBaseSkeletalMesh(bool ShowNormals);
	void DrawLodSkeletalMesh(const FStaticLODModel *lod);

	// skeleton configuration
	void SetBoneScale(const char *BoneName, float scale = 1.0f);
	//!! SetBone[Direction|Location|Rotation]()

	// animation control
	virtual void AnimStopLooping(int Channel);
	virtual void FreezeAnimAt(float Time, int Channel = 0);

	// animation state

	// get current animation information:
	// Frame     = 0..NumFrames
	// NumFrames = animation length, frames
	// Rate      = frames per seconds
	virtual void GetAnimParams(int Channel, const char *&AnimName,
		float &Frame, float &NumFrames, float &Rate) const;

	bool HasAnim(const char *AnimName) const
	{
		return FindAnim(AnimName) != INDEX_NONE;
	}
	virtual bool IsTweening(int Channel)
	{
		return GetStage(Channel).TweenTime > 0;
	}

	// animation blending
	void SetBlendParams(int Channel, float BlendAlpha, const char *BoneName = NULL);
	void SetBlendAlpha(int Channel, float BlendAlpha);
	void SetSecondaryAnim(int Channel, const char *AnimName = NULL);
	void SetSecondaryBlend(int Channel, float BlendAlpha);
	//?? -	AnimBlendToAlpha() - animate BlendAlpha coefficient
	//?? -	functions to smoothly replace current animation with another in a fixed time
	//??	(run new anim as secondary, ramp alpha from 0 to 1, and when blend becomes 1.0f
	//??	- replace 1st anim with 2nd, and clear 2nd

	// animation enumeration
	virtual int GetAnimCount() const
	{
		const USkeletalMesh *Mesh = GetMesh();
		if (!Mesh->Animation) return 0;
		return Mesh->Animation->AnimSeqs.Num();
	}
	virtual const char *GetAnimName(int Index) const
	{
		guard(CSkelMeshInstance::GetAnimName);
		if (Index < 0) return "None";
		const USkeletalMesh *Mesh = GetMesh();
		assert(Mesh->Animation);
		return Mesh->Animation->AnimSeqs[Index].Name;
		unguard;
	}

	virtual void UpdateAnimation(float TimeDelta);

protected:
	// mesh data
	struct CMeshBoneData *BoneData;
	struct CMeshWedge    *Wedges;	// bindpose
	struct CSkinVert     *Skinned;	// soft-skinned vertices
	CVec3		*InfColors;			// debug: color-by-influence for vertices
	int			LastLodNum;			// used to detect requirement to rebuild Wedges[]
	// animation state
	CAnimChan	Channels[MAX_SKELANIMCHANNELS];
	int			MaxAnimChannel;

	const USkeletalMesh *GetMesh() const
	{
		return static_cast<const USkeletalMesh*>(pMesh);
	}
	CAnimChan &GetStage(int StageIndex)
	{
		assert(StageIndex >= 0 && StageIndex < MAX_SKELANIMCHANNELS);
		return Channels[StageIndex];
	}
	const CAnimChan &GetStage(int StageIndex) const
	{
		assert(StageIndex >= 0 && StageIndex < MAX_SKELANIMCHANNELS);
		return Channels[StageIndex];
	}
	void TransformMesh(CMeshWedge *Wedges, int NumWedges);
	int FindBone(const char *BoneName) const;
	int FindAnim(const char *AnimName) const;
	virtual void PlayAnimInternal(const char *AnimName, float Rate, float TweenTime, int Channel, bool Looped);
	void UpdateSkeleton();
	void BuildInfColors();
};


/*-----------------------------------------------------------------------------
	CStatMeshInstance class
-----------------------------------------------------------------------------*/

class CStatMeshInstance : public CMeshInstance
{
public:
	UStaticMesh *pMesh;

	CStatMeshInstance()
	:	pMesh(NULL)
	{}

	void SetMesh(UStaticMesh *Mesh)
	{
		pMesh = Mesh;
	}

	virtual void Draw();
};


#endif // __MESHINSTANCE_H__